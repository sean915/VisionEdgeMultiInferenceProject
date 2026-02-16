#include "TrtSession.h"

#include <fstream>
#include <stdexcept>
#include <numeric>
#include <cstring>
#include <iostream>
#include <filesystem>

#include <opencv2/opencv.hpp>

#include "NvInferLegacyDims.h"   // Dims4
using namespace nvinfer1;

// TensorRT Dims에서 요소 개수 계산
// - 동적(-1) shape가 있으면 0 반환
static size_t vol_checked(const Dims& d) {
    size_t v = 1;
    for (int i = 0; i < d.nbDims; ++i) {
        if (d.d[i] <= 0) return 0;
        v *= (size_t)d.d[i];
    }
    return v;
}

// ORT와 동일한 방식의 float -> fp16 변환
// - TRT 입력/출력 FP16 변환 시 사용
static inline uint16_t float_to_fp16_bits_trt(float f) {
    union { float f; uint32_t u; } v;
    v.f = f;

    uint32_t x = v.u;
    uint32_t sign = (x >> 31) & 0x1;
    int32_t  exp = (int32_t)((x >> 23) & 0xFF) - 127;
    uint32_t mant = x & 0x7FFFFF;

    if (((x >> 23) & 0xFF) == 0xFF) {
        uint16_t hExp = 0x1F;
        uint16_t hMant = (mant ? 0x200 : 0x000);
        return (uint16_t)((sign << 15) | (hExp << 10) | hMant);
    }

    if (exp < -14) {
        return (uint16_t)(sign << 15);
    }

    if (exp > 15) {
        return (uint16_t)((sign << 15) | (0x1F << 10));
    }

    uint16_t hExp = (uint16_t)(exp + 15);
    uint16_t hMant = (uint16_t)(mant >> 13);
    return (uint16_t)((sign << 15) | (hExp << 10) | hMant);
}

// CUDA 호출 결과 체크 유틸
void TrtEngineWrap::checkCuda(cudaError_t e, const char* msg) {
    if (e != cudaSuccess) {
        throw std::runtime_error(std::string("[CUDA] ") + msg + " : " + cudaGetErrorString(e));
    }
}

// 엔진 파일 바이너리 로드
std::vector<char> TrtEngineWrap::readFile(const std::wstring& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) throw std::runtime_error("engine file open failed");
    f.seekg(0, std::ios::end);
    size_t sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> buf(sz);
    f.read(buf.data(), (std::streamsize)sz);
    return buf;
}

// TRT 엔진 래퍼 생성자
// - CUDA 스트림 생성
// - 엔진 로드
// - 입출력 버퍼 1차 할당
TrtEngineWrap::TrtEngineWrap(const std::wstring& enginePath, bool /*useCuda*/) {
    checkCuda(cudaStreamCreate(&stream_), "cudaStreamCreate");
    loadEngine(enginePath);
    allocateIO();
}

// CUDA 리소스 정리
TrtEngineWrap::~TrtEngineWrap() {
    if (dInput_) cudaFree(dInput_);
    for (auto p : dOutputs_) if (p) cudaFree(p);
    if (stream_) cudaStreamDestroy(stream_);
}

// TRT 엔진 로드 + 실행 컨텍스트 생성
void TrtEngineWrap::loadEngine(const std::wstring& enginePath) {
    struct SimpleLogger : ILogger {
        void log(Severity s, const char* m) noexcept override {
            if (s <= Severity::kWARNING) { (void)m; }
        }
    };
    static SimpleLogger gLogger;
    logger_ = &gLogger;

    auto bytes = readFile(enginePath);

    runtime_.reset(createInferRuntime(*logger_));
    if (!runtime_) throw std::runtime_error("createInferRuntime failed");

    engine_.reset(runtime_->deserializeCudaEngine(bytes.data(), bytes.size()));
    if (!engine_) throw std::runtime_error("deserializeCudaEngine failed");

    context_.reset(engine_->createExecutionContext());
    if (!context_) throw std::runtime_error("createExecutionContext failed");

    inputIndex_ = -1;
    outputIndices_.clear();

    int nbIOTensors = engine_->getNbIOTensors();
    for (int i = 0; i < nbIOTensors; ++i) {
        const char* name = engine_->getIOTensorName(i);
        auto mode = engine_->getTensorIOMode(name);
        if (mode == TensorIOMode::kINPUT) inputIndex_ = i;
        else outputIndices_.push_back(i);
    }
    if (inputIndex_ < 0 || outputIndices_.empty())
        throw std::runtime_error("failed to find input/output tensors");
}

// IO 버퍼 1차 할당 (정적 shape 대응)
void TrtEngineWrap::allocateIO() {
    const char* inName = engine_->getIOTensorName(inputIndex_);
    Dims inDims = engine_->getTensorShape(inName);

    size_t inElem = vol_checked(inDims);
    if (inElem > 0) {
        hostInputFp32_.resize(inElem);

        DataType inDt = engine_->getTensorDataType(inName);
        size_t needBytes = 0;
        if (inDt == DataType::kFLOAT) needBytes = inElem * sizeof(float);
        else if (inDt == DataType::kHALF) needBytes = inElem * sizeof(uint16_t);
        else throw std::runtime_error("unsupported input dtype (need add int8/int32 etc)");

        if (dInput_) cudaFree(dInput_);
        dInput_ = nullptr;
        checkCuda(cudaMalloc(&dInput_, needBytes), "cudaMalloc dInput");
        dInputBytes_ = needBytes;
    }

    dOutputs_.resize(outputIndices_.size(), nullptr);
    dOutputBytes_.assign(outputIndices_.size(), 0);

    for (size_t oi = 0; oi < outputIndices_.size(); ++oi) {
        const char* outName = engine_->getIOTensorName(outputIndices_[oi]);
        Dims outDims = engine_->getTensorShape(outName);
        size_t outElem = vol_checked(outDims);
        if (outElem == 0) continue;

        DataType dt = engine_->getTensorDataType(outName);
        size_t bytes = 0;
        if (dt == DataType::kFLOAT) bytes = outElem * sizeof(float);
        else if (dt == DataType::kHALF) bytes = outElem * sizeof(uint16_t);
        else throw std::runtime_error("unsupported output dtype (need add int8/int32 etc)");

        if (dOutputs_[oi]) cudaFree(dOutputs_[oi]);
        dOutputs_[oi] = nullptr;
        checkCuda(cudaMalloc(&dOutputs_[oi], bytes), "cudaMalloc dOutput");
        dOutputBytes_[oi] = bytes;
    }
}

// 입력 이미지를 8UC3(BGR)로 보정
static inline cv::Mat EnsureBgr3_U8(const cv::Mat& src) {
    if (src.empty()) return cv::Mat();

    if (src.type() == CV_8UC3) return src;

    cv::Mat bgr3;
    if (src.type() == CV_8UC4) {
        cv::cvtColor(src, bgr3, cv::COLOR_BGRA2BGR);
    }
    else if (src.type() == CV_8UC1) {
        cv::cvtColor(src, bgr3, cv::COLOR_GRAY2BGR);
    }
    else {
        throw std::runtime_error("TrtEngineWrap::run: unsupported cv::Mat type (need 8U 1/3/4 channels)");
    }
    return bgr3;
}

// TRT 추론 실행 (ORT와 동일 전처리 흐름)
bool TrtEngineWrap::run(const cv::Mat& bgr,
    int inW, int inH,
    bool useFp16Input,
    std::vector<uint16_t>& outFp16,
    std::vector<float>& outFp32,
    std::vector<int64_t>* outShape,
    bool /*outputFp16Preferred*/)
{
    if (bgr.empty()) return false;

    // 0) 입력 타입 보정
    cv::Mat bgr3 = EnsureBgr3_U8(bgr);

    // 1) 리사이즈
    cv::Mat resized;
    if (bgr3.cols != inW || bgr3.rows != inH) {
        cv::resize(bgr3, resized, cv::Size(inW, inH), 0, 0, cv::INTER_LINEAR);
    }
    else {
        resized = bgr3;
    }
    if (!resized.isContinuous()) resized = resized.clone();

    // 2) 동적 shape일 경우 입력 shape 지정
    const char* inName = engine_->getIOTensorName(inputIndex_);
    Dims cur = context_->getTensorShape(inName);
    bool needSetShape = false;
    for (int i = 0; i < cur.nbDims; ++i) {
        if (cur.d[i] <= 0) { needSetShape = true; break; }
    }
    if (needSetShape) {
        bool ok = context_->setInputShape(inName, Dims4{ 1, 3, inH, inW });
        if (!ok) throw std::runtime_error("setInputShape failed (check engine expects NCHW 1x3xHxW)");
    }

    Dims inDims = context_->getTensorShape(inName);
    size_t inElem = vol_checked(inDims);
    if (inElem == 0) {
        throw std::runtime_error("input dims unresolved (dynamic shape still has -1?)");
    }

    // 3) BGR->RGB + 0~1 + NCHW (R,G,B)
    const int H = inH, W = inW;
    const size_t wantElem = (size_t)1 * 3u * (size_t)inH * (size_t)inW;
    if (inElem != wantElem) {
        throw std::runtime_error("input elem mismatch: engine input != 1*3*inH*inW (check engine input layout)");
    }
    hostInputFp32_.resize(inElem);

    for (int y = 0; y < H; ++y) {
        const cv::Vec3b* row = resized.ptr<cv::Vec3b>(y);
        const int base = y * W;
        for (int x = 0; x < W; ++x) {
            const cv::Vec3b p = row[x]; // BGR
            const float b = p[0] * (1.0f / 255.0f);
            const float g = p[1] * (1.0f / 255.0f);
            const float r = p[2] * (1.0f / 255.0f);

            const int idx = base + x;
            hostInputFp32_[0 * (H * W) + idx] = r;
            hostInputFp32_[1 * (H * W) + idx] = g;
            hostInputFp32_[2 * (H * W) + idx] = b;
        }
    }

    // 4) 입력 dtype 결정 + Host->Device 복사
    DataType inDt = engine_->getTensorDataType(inName);

    bool doFp16 = false;
    if (inDt == DataType::kHALF) doFp16 = true;
    else if (inDt == DataType::kFLOAT) doFp16 = false;
    else throw std::runtime_error("unsupported input dtype");

    if (useFp16Input && inDt == DataType::kHALF) doFp16 = true;

    size_t needInBytes = inElem * (doFp16 ? sizeof(uint16_t) : sizeof(float));
    if (!dInput_ || dInputBytes_ != needInBytes) {
        if (dInput_) cudaFree(dInput_);
        dInput_ = nullptr;
        checkCuda(cudaMalloc(&dInput_, needInBytes), "cudaMalloc dInput (resize)");
        dInputBytes_ = needInBytes;
    }

    if (!doFp16) {
        checkCuda(cudaMemcpyAsync(dInput_, hostInputFp32_.data(),
            inElem * sizeof(float),
            cudaMemcpyHostToDevice, stream_),
            "cudaMemcpyAsync H2D input fp32");
    }
    else {
        hostInputFp16_.resize(inElem);
        for (size_t i = 0; i < inElem; ++i) {
            hostInputFp16_[i] = float_to_fp16_bits_trt(hostInputFp32_[i]);
        }
        checkCuda(cudaMemcpyAsync(dInput_, hostInputFp16_.data(),
            inElem * sizeof(uint16_t),
            cudaMemcpyHostToDevice, stream_),
            "cudaMemcpyAsync H2D input fp16");
    }

    // 5) 출력 버퍼 재할당(동적 shape 대응)
    for (size_t oi = 0; oi < outputIndices_.size(); ++oi) {
        const char* outName = engine_->getIOTensorName(outputIndices_[oi]);
        Dims outDims = context_->getTensorShape(outName);
        size_t outElem = vol_checked(outDims);
        if (outElem == 0) throw std::runtime_error("output dims unresolved (dynamic?)");

        DataType dt = engine_->getTensorDataType(outName);
        size_t bytes = 0;
        if (dt == DataType::kFLOAT) bytes = outElem * sizeof(float);
        else if (dt == DataType::kHALF) bytes = outElem * sizeof(uint16_t);
        else throw std::runtime_error("unsupported output dtype");

        if (!dOutputs_[oi] || dOutputBytes_[oi] != bytes) {
            if (dOutputs_[oi]) cudaFree(dOutputs_[oi]);
            dOutputs_[oi] = nullptr;
            checkCuda(cudaMalloc(&dOutputs_[oi], bytes), "cudaMalloc dOutput (resize)");
            dOutputBytes_[oi] = bytes;
        }
    }

    // 6) 텐서 주소 바인딩
    context_->setTensorAddress(inName, dInput_);
    for (size_t oi = 0; oi < outputIndices_.size(); ++oi) {
        const char* outName = engine_->getIOTensorName(outputIndices_[oi]);
        context_->setTensorAddress(outName, dOutputs_[oi]);
    }

    // 7) 실행
    if (!context_->enqueueV3(stream_)) return false;

    // 8) output0만 Host로 복사
    const char* outName0 = engine_->getIOTensorName(outputIndices_[0]);
    Dims outDims0 = context_->getTensorShape(outName0);
    size_t outElem0 = vol_checked(outDims0);
    if (outElem0 == 0) return false;

    if (outShape) {
        outShape->clear();
        outShape->reserve((size_t)outDims0.nbDims);
        for (int i = 0; i < outDims0.nbDims; ++i) outShape->push_back(outDims0.d[i]);
    }

    DataType dt0 = engine_->getTensorDataType(outName0);

    if (dt0 == DataType::kFLOAT) {
        outFp32.resize(outElem0);
        outFp16.clear();
        checkCuda(cudaMemcpyAsync(outFp32.data(), dOutputs_[0],
            outElem0 * sizeof(float),
            cudaMemcpyDeviceToHost, stream_),
            "cudaMemcpyAsync D2H output fp32");
    }
    else if (dt0 == DataType::kHALF) {
        outFp16.resize(outElem0);
        outFp32.clear();
        checkCuda(cudaMemcpyAsync(outFp16.data(), dOutputs_[0],
            outElem0 * sizeof(uint16_t),
            cudaMemcpyDeviceToHost, stream_),
            "cudaMemcpyAsync D2H output fp16");
    }
    else {
        throw std::runtime_error("unsupported output dtype");
    }

    checkCuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize");
    return true;
}

// TRT 엔진 IO 정보 출력 (디버그용)
void TrtEngineWrap::dumpIoInfo() const {
    int nb = engine_->getNbIOTensors();
    std::cout << "\n========== TRT IO Info ==========\n";
    for (int i = 0; i < nb; ++i) {
        const char* n = engine_->getIOTensorName(i);
        auto mode = engine_->getTensorIOMode(n);
        auto dt = engine_->getTensorDataType(n);
        auto sh = engine_->getTensorShape(n);

        std::cout << "  [" << i << "] "
            << (mode == TensorIOMode::kINPUT ? "IN " : "OUT")
            << " name=" << n
            << " dtype=" << (dt == DataType::kFLOAT ? "FLOAT" : dt == DataType::kHALF ? "HALF" : "OTHER")
            << " shape=[";
        for (int k = 0; k < sh.nbDims; ++k) {
            std::cout << sh.d[k] << (k + 1 < sh.nbDims ? "," : "");
        }
        std::cout << "]\n";
    }
    std::cout << "=================================\n";
}