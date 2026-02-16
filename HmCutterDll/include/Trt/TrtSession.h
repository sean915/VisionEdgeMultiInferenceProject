// TrtEngineWrap.h  (TrtEngineWrap.cpp 최신 버전에 맞춘 복붙 헤더)
//
// ✅ 변경사항 요약(기능 변화 없음 / .cpp와 일치):
// - dInputBytes_ 추가: 입력 버퍼 재할당 체크용
// - dOutputBytes_ 추가: 출력 버퍼 재할당 체크용(출력 여러 개 대비)
// - hostInputFp16_ 유지(ORT와 동일 fp32->fp16 bits 변환 후 업로드용)
// - include 정리(필요 최소)
// - 복사/대입 금지(디바이스 포인터 보유 클래스라 안전)

#pragma once

#include <string>
#include <vector>
#include <memory>

#include <opencv2/opencv.hpp>

#include <NvInfer.h>
#include <cuda_runtime.h>

class TrtEngineWrap {
public:
    explicit TrtEngineWrap(const std::wstring& enginePath, bool useCuda = true);
    ~TrtEngineWrap();

    TrtEngineWrap(const TrtEngineWrap&) = delete;
    TrtEngineWrap& operator=(const TrtEngineWrap&) = delete;
    TrtEngineWrap(TrtEngineWrap&&) = delete;
    TrtEngineWrap& operator=(TrtEngineWrap&&) = delete;

    // ORT run() 패턴과 맞추기 위한 인터페이스:
    // - 입력: cv::Mat(BGR 계열), inW/inH
    // - useFp16Input: 엔진 입력 dtype이 HALF인 경우 fp16 업로드(또는 요청 시)
    // - 출력: output0만 outFp16 또는 outFp32에 담음
    // - outShape: output0 shape 반환(옵션)
    bool run(const cv::Mat& bgr,
        int inW, int inH,
        bool useFp16Input,
        std::vector<uint16_t>& outFp16,
        std::vector<float>& outFp32,
        std::vector<int64_t>* outShape = nullptr,
        bool outputFp16Preferred = false);

    // 디버그용(IO name/dtype/shape)
    void dumpIoInfo() const;

private:
    void loadEngine(const std::wstring& enginePath);
    void allocateIO();

    static std::vector<char> readFile(const std::wstring& path);
    static void checkCuda(cudaError_t e, const char* msg);

private:
    // TRT core
    nvinfer1::ILogger* logger_ = nullptr;
    std::unique_ptr<nvinfer1::IRuntime> runtime_;
    std::unique_ptr<nvinfer1::ICudaEngine> engine_;
    std::unique_ptr<nvinfer1::IExecutionContext> context_;

    cudaStream_t stream_{}; // cuda stream

    // IO tensor indices (TensorRT 8.5+/10 style getIOTensorName)
    int inputIndex_ = -1;
    std::vector<int> outputIndices_;

    // device buffers
    void* dInput_ = nullptr;
    size_t dInputBytes_ = 0;                 // ✅ .cpp에서 사용
    std::vector<void*> dOutputs_;
    std::vector<size_t> dOutputBytes_;       // ✅ .cpp에서 사용

    // host scratch
    std::vector<float> hostInputFp32_;       // NCHW fp32
    std::vector<uint16_t> hostInputFp16_;    // fp16 bits (ORT와 동일 변환)
};
