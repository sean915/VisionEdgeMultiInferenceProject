#include "DetectorParsers.h"
#include "DetectorHelpers.h"
#include <onnxruntime_cxx_api.h>

namespace HmCutter {

    // ✅ 동적 클래스 수 지원: ORT 출력 텐서에서 numClasses개의 확률을 읽어옴
    bool ParseDefectProbs_Ort(
        const std::vector<Ort::Value>& outs,
        int numClasses,
        std::vector<float>& probs)
    {
        probs.assign(numClasses, 0.f);
        if (outs.empty() || numClasses <= 0) return false;

        const Ort::Value* ov = nullptr;
        for (const auto& v : outs) {
            if (v.IsTensor()) { ov = &v; break; }
        }
        if (!ov) return false;

        auto ti = ov->GetTensorTypeAndShapeInfo();
        if (ti.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) return false;

        const int64_t elem = ti.GetElementCount();
        if (elem < numClasses) return false;

        const float* data = ov->GetTensorData<float>();
        for (int i = 0; i < numClasses; ++i)
            probs[i] = data[i];

        return true;
    }

    // ✅ 동적 클래스 수 지원: TRT 출력 버퍼에서 numClasses개의 확률을 읽어옴
    bool ParseDefectProbs_Trt(
        const std::vector<uint16_t>& outFp16,
        const std::vector<float>& outFp32,
        const std::vector<int64_t>& /*outShape*/,
        int numClasses,
        std::vector<float>& probs)
    {
        probs.assign(numClasses, 0.f);
        if (numClasses <= 0) return false;

        const bool isFp32 = !outFp32.empty();
        const bool isFp16 = !outFp16.empty();

        if (!isFp32 && !isFp16) return false;

        if (isFp32) {
            if ((int)outFp32.size() < numClasses) return false;
            for (int i = 0; i < numClasses; ++i)
                probs[i] = outFp32[i];
            return true;
        }
        else {
            if ((int)outFp16.size() < numClasses) return false;
            for (int i = 0; i < numClasses; ++i)
                probs[i] = fp16_to_fp32(outFp16[i]);
            return true;
        }
    }

} // namespace HmCutter