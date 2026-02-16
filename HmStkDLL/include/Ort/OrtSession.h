// OrtSession.h (추가/수정 예시)
#pragma once
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

class OrtSessionWrap {
public:
    OrtSessionWrap(const std::wstring & modelPath, bool useCuda);

    // 기본 생성자 필요시 명시적으로 삭제
    OrtSessionWrap() = delete;

    Ort::Session session_ ;

    //// 기존 run (그대로 유지)
    //std::vector<Ort::Value> run(const std::vector<const char*>& inNames,
    //    const std::vector<Ort::Value>& inVals,
    //    const std::vector<const char*>& outNames);

    // ✅ 추가: cv::Mat(BGR) 넣으면 내부에서 텐서 만들어서 Run까지
    std::vector<Ort::Value> run(const cv::Mat& bgr,
        int inW, int inH,
        bool useFp16,
        std::vector<uint16_t>& fp16buf,
        std::vector<float>& fp32buf);

    static const char* ElemTypeStr(ONNXTensorElementDataType t);
    void dumpModelInfo(const char* tag);

    size_t inputCount() const { return session_.GetInputCount(); }
    size_t outputCount() const { return session_.GetOutputCount(); }


private:
    Ort::Env env_;
    Ort::SessionOptions opts_;
    Ort::MemoryInfo memInfo_{ nullptr };

    std::vector<std::string> inNameStr_;
    std::vector<const char*> inNames_;
    std::vector<std::string> outNameStr_;
    std::vector<const char*> outNames_;

private:
    static uint16_t float_to_fp16_bits(float f);
};
