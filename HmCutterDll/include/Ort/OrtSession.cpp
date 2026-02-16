// OrtSession.cpp
#include "OrtSession.h"
#include <stdexcept>
#include <array>
#include <opencv2/opencv.hpp>
#include <cstring>   // memcpy

#define USE_ORT_CUDA

// ONNX Runtime 세션 래퍼 생성자
// - 환경/세션 옵션 설정
// - CUDA 사용 옵션 적용
// - 입력/출력 이름 캐시
OrtSessionWrap::OrtSessionWrap(const std::wstring& modelPath, bool useCuda)
    : env_(ORT_LOGGING_LEVEL_WARNING, "hmstack-ort")
    , session_(nullptr)
    , memInfo_(nullptr)
{
    if (modelPath.empty()) {
        throw std::runtime_error("OrtSessionWrap: modelPath is empty");
    }

    // 세션 옵션: 스레드/최적화 설정
    opts_.SetIntraOpNumThreads(1);
    opts_.SetInterOpNumThreads(1);
    opts_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

#ifdef USE_ORT_CUDA
    // CUDA 실행 옵션 추가 (선택)
    if (useCuda) {
        OrtCUDAProviderOptions cuda_opts{};
        opts_.AppendExecutionProvider_CUDA(cuda_opts);
    }
#else
    (void)useCuda;
#endif

    // 세션 생성
    session_ = Ort::Session(env_, modelPath.c_str(), opts_);

    // CPU 메모리 정보 (CreateTensorWithDataAsOrtValue 사용 대비)
    memInfo_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // IO 이름 캐시
    Ort::AllocatorWithDefaultOptions allocator;

    // 입력 이름 캐시
    const size_t inCount = session_.GetInputCount();
    inNameStr_.clear();
    inNames_.clear();
    inNameStr_.reserve(inCount);
    inNames_.reserve(inCount);

    for (size_t i = 0; i < inCount; ++i) {
        Ort::AllocatedStringPtr name = session_.GetInputNameAllocated(i, allocator);
        inNameStr_.emplace_back(name.get());
    }
    for (auto& s : inNameStr_) inNames_.push_back(s.c_str());

    // 출력 이름 캐시
    const size_t outCount = session_.GetOutputCount();
    outNameStr_.clear();
    outNames_.clear();
    outNameStr_.reserve(outCount);
    outNames_.reserve(outCount);

    for (size_t i = 0; i < outCount; ++i) {
        Ort::AllocatedStringPtr name = session_.GetOutputNameAllocated(i, allocator);
        outNameStr_.emplace_back(name.get());
    }
    for (auto& s : outNameStr_) outNames_.push_back(s.c_str());
}

// ORT 추론 실행
// - 입력 이미지 전처리(BGR->RGB, NCHW, 0~1)
// - FP32/FP16 텐서 생성
// - session_.Run 호출 후 출력 반환
std::vector<Ort::Value> OrtSessionWrap::run(const cv::Mat& bgr,
    int inW, int inH,
    bool useFp16,
    std::vector<uint16_t>& fp16buf,
    std::vector<float>& fp32buf)
{
    // 세션/이름 검증
    if (inNames_.empty())  throw std::runtime_error("OrtSessionWrap::run: inNames_ empty (session not initialized?)");
    if (outNames_.empty()) throw std::runtime_error("OrtSessionWrap::run: outNames_ empty (session not initialized?)");

    if (bgr.empty()) throw std::runtime_error("OrtSessionWrap::run: empty image");

    // 입력 타입 보정 (BGR 3채널로 통일)
    cv::Mat bgr3;
    if (bgr.type() == CV_8UC3) {
        bgr3 = bgr;
    }
    else if (bgr.type() == CV_8UC4) {
        cv::cvtColor(bgr, bgr3, cv::COLOR_BGRA2BGR);
    }
    else if (bgr.type() == CV_8UC1) {
        cv::cvtColor(bgr, bgr3, cv::COLOR_GRAY2BGR);
    }
    else {
        throw std::runtime_error("OrtSessionWrap::run: unsupported cv::Mat type (need 8U 1/3/4 channels)");
    }

    // 1) 리사이즈
    cv::Mat resized;
    if (bgr3.cols != inW || bgr3.rows != inH) {
        cv::resize(bgr3, resized, cv::Size(inW, inH), 0, 0, cv::INTER_LINEAR);
    }
    else {
        resized = bgr3;
    }

    // 연속 메모리 보장
    if (!resized.isContinuous()) resized = resized.clone();

    // 2) NCHW float 버퍼 생성 (BGR->RGB, 0~1)
    const int H = inH, W = inW;
    const int64_t N = 1, C = 3;
    const int64_t tensorSize = N * C * H * W;
    const std::array<int64_t, 4> shape{ N, C, H, W };

    fp32buf.resize((size_t)tensorSize);

    for (int y = 0; y < H; ++y) {
        const cv::Vec3b* row = resized.ptr<cv::Vec3b>(y);
        const int base = y * W;
        for (int x = 0; x < W; ++x) {
            const cv::Vec3b p = row[x]; // BGR
            const float b = p[0] * (1.0f / 255.0f);
            const float g = p[1] * (1.0f / 255.0f);
            const float r = p[2] * (1.0f / 255.0f);

            const int idx = base + x;
            fp32buf[0 * (H * W) + idx] = r;
            fp32buf[1 * (H * W) + idx] = g;
            fp32buf[2 * (H * W) + idx] = b;
        }
    }

    // 3) 입력 텐서 생성 (FP32/FP16)
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::Value inTensor{ nullptr };

    if (!useFp16) {
        inTensor = Ort::Value::CreateTensor<float>(allocator, shape.data(), shape.size());
        float* dst = inTensor.GetTensorMutableData<float>();
        std::memcpy(dst, fp32buf.data(), fp32buf.size() * sizeof(float));
    }
    else {
        // fp32 -> fp16 변환
        fp16buf.resize((size_t)tensorSize);
        for (size_t i = 0; i < fp32buf.size(); ++i) {
            fp16buf[i] = float_to_fp16_bits(fp32buf[i]);
        }

        inTensor = Ort::Value::CreateTensor<Ort::Float16_t>(allocator, shape.data(), shape.size());
        auto* dst16 = inTensor.GetTensorMutableData<Ort::Float16_t>();
        std::memcpy(dst16, fp16buf.data(), fp16buf.size() * sizeof(uint16_t));
    }

    // 4) Run 실행
    std::vector<Ort::Value> inVals;
    inVals.emplace_back(std::move(inTensor));

    if (inVals.size() != inNames_.size()) {
        throw std::runtime_error("OrtSessionWrap::run: input count mismatch (inNames_.size != 1)");
    }

    auto out = session_.Run(
        Ort::RunOptions{ nullptr },
        inNames_.data(), inVals.data(), inVals.size(),
        outNames_.data(), outNames_.size()
    );
    return out;
}

// ONNX 텐서 타입을 문자열로 변환
const char* OrtSessionWrap::ElemTypeStr(ONNXTensorElementDataType t)
{
    switch (t) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:   return "FLOAT32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return "FLOAT16";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:   return "INT64";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:   return "INT32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:   return "UINT8";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:    return "INT8";
    default: return "OTHER";
    }
}

// 모델 입력/출력 정보 출력 (디버그용)
void OrtSessionWrap::dumpModelInfo(const char* tag)
{
    std::cout << "\n========== ORT Model Info: " << tag << " ==========\n";
    std::cout << "Inputs : " << OrtSessionWrap::session_.GetInputCount() << "\n";
    std::cout << "Outputs: " << OrtSessionWrap::session_.GetOutputCount() << "\n";

    Ort::AllocatorWithDefaultOptions allocator;

    // Inputs
    for (size_t i = 0; i < OrtSessionWrap::session_.GetInputCount(); ++i) {
        auto name = OrtSessionWrap::session_.GetInputNameAllocated(i, allocator);

        auto typeInfo = OrtSessionWrap::session_.GetInputTypeInfo(i);
        auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
        auto elemType = tensorInfo.GetElementType();
        auto shape = tensorInfo.GetShape();

        std::cout << "  [IN " << i << "] name=" << name.get()
            << " type=" << ElemTypeStr(elemType)
            << " shape=[";
        for (size_t k = 0; k < shape.size(); ++k) {
            std::cout << shape[k] << (k + 1 < shape.size() ? "," : "");
        }
        std::cout << "]\n";
    }

    // Outputs
    for (size_t i = 0; i < OrtSessionWrap::session_.GetOutputCount(); ++i) {
        auto name = OrtSessionWrap::session_.GetOutputNameAllocated(i, allocator);

        auto typeInfo = OrtSessionWrap::session_.GetOutputTypeInfo(i);
        auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
        auto elemType = tensorInfo.GetElementType();
        auto shape = tensorInfo.GetShape();

        std::cout << "  [OUT " << i << "] name=" << name.get()
            << " type=" << ElemTypeStr(elemType)
            << " shape=[";
        for (size_t k = 0; k < shape.size(); ++k) {
            std::cout << shape[k] << (k + 1 < shape.size() ? "," : "");
        }
        std::cout << "]\n";
    }

    std::cout << "==============================================\n";
}

// float -> fp16 비트 변환 (간단 구현)
// - FP16 입력 텐서 생성 시 사용
uint16_t OrtSessionWrap::float_to_fp16_bits(float f) {
    union { float f; uint32_t u; } v;
    v.f = f;

    uint32_t x = v.u;
    uint32_t sign = (x >> 31) & 0x1;
    int32_t  exp = (int32_t)((x >> 23) & 0xFF) - 127;
    uint32_t mant = x & 0x7FFFFF;

    // NaN/Inf 처리
    if (((x >> 23) & 0xFF) == 0xFF) {
        uint16_t hExp = 0x1F;
        uint16_t hMant = (mant ? 0x200 : 0x000);
        return (uint16_t)((sign << 15) | (hExp << 10) | hMant);
    }

    // underflow
    if (exp < -14) {
        return (uint16_t)(sign << 15);
    }

    // overflow
    if (exp > 15) {
        return (uint16_t)((sign << 15) | (0x1F << 10));
    }

    uint16_t hExp = (uint16_t)(exp + 15);
    uint16_t hMant = (uint16_t)(mant >> 13);
    return (uint16_t)((sign << 15) | (hExp << 10) | hMant);
}