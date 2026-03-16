#pragma once
#include <vector>
#include <cstdint>
#include <onnxruntime_cxx_api.h>

namespace HmCutter {

    // ✅ 동적 클래스 수 지원: TRT 출력 버퍼에서 numClasses개의 확률을 읽어옴
    bool ParseDefectProbs_Trt(
        const std::vector<uint16_t>& outFp16,
        const std::vector<float>& outFp32,
        const std::vector<int64_t>& outShape,
        int numClasses,
        std::vector<float>& probs);

    // ✅ 동적 클래스 수 지원: ORT 출력 텐서에서 numClasses개의 확률을 읽어옴
    bool ParseDefectProbs_Ort(
        const std::vector<Ort::Value>& outs,
        int numClasses,
        std::vector<float>& probs);

} // namespace HmCutter