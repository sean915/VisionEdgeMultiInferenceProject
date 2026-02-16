#pragma once
#include <vector>
#include <cstdint>
#include <onnxruntime_cxx_api.h>

namespace HmCutter {

    bool ParseDefectProbs3_Ort(
        const std::vector<Ort::Value>& outs,
        float& p_ab,
        float& p_no
        );

    bool ParseDefectProbs3_Trt(
        const std::vector<uint16_t>& outFp16,
        const std::vector<float>& outFp32,
        const std::vector<int64_t>& outShape,
        float& p_ab,
        float& p_no
        );

} // namespace HMSTACK