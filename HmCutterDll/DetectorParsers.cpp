#include "DetectorParsers.h"
#include "DetectorHelpers.h"
#include <onnxruntime_cxx_api.h>

namespace HmCutter {

    // ORT 출력 텐서에서 defect 확률 3개(abnormal/normal/empty)를 읽어오는 함수
  // - outs: OrtSession 실행 결과 벡터
  // - p_ab/p_no/p_em: 확률 출력 버퍼
  // - 반환값: 성공(true) / 실패(false)
    bool ParseDefectProbs3_Ort(const std::vector<Ort::Value>& outs, float& p_ab, float& p_no)
    {
        p_ab = p_no = 0.f;
        if (outs.empty()) return false;

        const Ort::Value* ov = nullptr;
        for (const auto& v : outs) {
            if (v.IsTensor()) { ov = &v; break; }
        }
        if (!ov) return false;

        auto ti = ov->GetTensorTypeAndShapeInfo();
        if (ti.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) return false;

        const int64_t elem = ti.GetElementCount();
        if (elem < 2) return false;

        const float* data = ov->GetTensorData<float>();

        // 출력 순서: [abnormal, normal]
        p_ab = data[0]; p_no = data[1];
        return true;
    }

    // TRT 출력 버퍼에서 defect 확률 3개를 읽어오는 함수
  // - outFp16/outFp32 중 하나가 채워져 있음
  // - fp16이면 fp16_to_fp32로 변환
  // - 반환값: 성공(true) / 실패(false)

    bool ParseDefectProbs3_Trt(
        const std::vector<uint16_t>& outFp16,
        const std::vector<float>& outFp32,
        const std::vector<int64_t>& /*outShape*/,
        float& p_ab, float& p_no)
    {
        p_ab = p_no =  0.f;

        const bool isFp32 = !outFp32.empty();
        const bool isFp16 = !outFp16.empty();

        if (!isFp32 && !isFp16) return false;

        if (isFp32) {
            if (outFp32.size() < 2) return false;
            p_ab = outFp32[0]; p_no = outFp32[1]; 
            return true;
        }
        else {
            if (outFp16.size() < 2) return false;
            p_ab = fp16_to_fp32(outFp16[0]);
            p_no = fp16_to_fp32(outFp16[1]);
           
            return true;
        }
    }

} // namespace HMSTACK