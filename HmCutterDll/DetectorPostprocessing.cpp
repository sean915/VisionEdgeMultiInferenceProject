#include "DetectorPostprocessing.h"
#include "DetectorHelpers.h"
#include <algorithm>
#include <cmath>

namespace HmCutter {

    // 두 박스(TLXYWH 기준)의 IoU 계산
    // NMS에서 겹침 정도 판단에 사용됨
    static inline void Softmax2(float a, float b, float& outA, float& outB)
    {
        const float m = std::max(a, b);
        const float ea = std::exp(a - m);
        const float eb = std::exp(b - m);
        const float s = ea + eb;
        outA = ea / s;
        outB = eb / s;
    }

    float IoU_TLXYWH(const cv::Rect2f& a, const cv::Rect2f& b)
    {
        float ax1 = a.x, ay1 = a.y, ax2 = a.x + a.width, ay2 = a.y + a.height;
        float bx1 = b.x, by1 = b.y, bx2 = b.x + b.width, by2 = b.y + b.height;
        float xx1 = std::max(ax1, bx1), yy1 = std::max(ay1, by1);
        float xx2 = std::min(ax2, bx2), yy2 = std::min(ay2, by2);
        float w = std::max(0.f, xx2 - xx1);
        float h = std::max(0.f, yy2 - yy1);
        float inter = w * h;
        float uni = a.area() + b.area() - inter;
        return (uni <= 0.f) ? 0.f : (inter / uni);
    }

    // 클래스별 NMS 수행
    // - score 내림차순 정렬 후 IoU 기준으로 중복 제거
    std::vector<DetCand> NmsPerClass(std::vector<DetCand> cands, float iouThr)
    {
        std::sort(cands.begin(), cands.end(),
            [](const DetCand& a, const DetCand& b) { return a.score > b.score; });

        std::vector<DetCand> out;
        std::vector<char> sup(cands.size(), 0);

        for (size_t i = 0; i < cands.size(); ++i) {
            if (sup[i]) continue;
            out.push_back(cands[i]);

            for (size_t j = i + 1; j < cands.size(); ++j) {
                if (sup[j]) continue;
                if (cands[j].cls != cands[i].cls) continue;
                if (IoU_TLXYWH(cands[i].box, cands[j].box) > iouThr) sup[j] = 1;
            }
        }
        return out;
    }

    // TRT 출력 버퍼에서 트리거 결과 하나의 값 읽기 (fp16/fp32 자동 처리)
    // - outFp32가 있으면 그대로 사용
    // - 없으면 fp16 -> fp32 변환
    static bool GetTrigVal_Trt(const std::vector<uint16_t>& outFp16,
        const std::vector<float>& outFp32,
        int64_t idx,
        float& v)
    {
        if (!outFp32.empty()) {
            if ((size_t)idx >= outFp32.size()) return false;
            v = outFp32[(size_t)idx];
            return true;
        }
        if (!outFp16.empty()) {
            if ((size_t)idx >= outFp16.size()) return false;
            v = fp16_to_fp32(outFp16[(size_t)idx]);
            return true;
        }
        return false;
    }

    // TRT 트리거 출력 -> DefectJob 변환
    // - 출력 shape를 해석해 [cx,cy,w,h,score1,score2] 배열로 변환
    // - tab/horn 각각 최고 score 박스를 추출
    // - letterbox 좌표를 원본 프레임 좌표로 변환
    void MapTriggerOutsToJob_Letterbox_Trt(
        const std::vector<uint16_t>& outFp16,
        const std::vector<float>& outFp32,
        const std::vector<int64_t>& outShape,
        const LetterboxInfo& lb,
        int modelW, int modelH,
        int frameW, int frameH,
        float tab_min_score, float horn_min_score,
        float nms_iou_thr,
        HmCutter::Detector::DefectJob& job)
    {
        (void)modelW; (void)modelH;

        job.trig.ok = false;
        job.trig.tab = {};
        job.trig.horn = {};
        job.trig.tab_score = 0.f;
        job.trig.horn_score = 0.f;

        const size_t elemCount = !outFp32.empty() ? outFp32.size() : outFp16.size();
        if (elemCount == 0) return;

        enum Layout { CHW_1x6xN, ROW_1xNx6, ROW_Nx6, UNKNOWN };
        Layout layout = UNKNOWN;
        int64_t N = 0;

        // 출력 shape 자동 판별
        if (outShape.size() == 3 && outShape[0] == 1 && outShape[1] == 6 && outShape[2] > 0) {
            layout = CHW_1x6xN;
            N = outShape[2];
        }
        else if (outShape.size() == 3 && outShape[0] == 1 && outShape[2] == 6 && outShape[1] > 0) {
            layout = ROW_1xNx6;
            N = outShape[1];
        }
        else if (outShape.size() == 2 && outShape[1] == 6 && outShape[0] > 0) {
            layout = ROW_Nx6;
            N = outShape[0];
        }
        else if (outShape.size() == 1 && outShape[0] > 0 && (outShape[0] % 6) == 0) {
            layout = ROW_Nx6;
            N = outShape[0] / 6;
        }
        else {
            if ((elemCount % 6) == 0) {
                layout = ROW_Nx6;
                N = (int64_t)(elemCount / 6);
            }
            else {
                return;
            }
        }

        auto get_val = [&](int ch, int64_t i)->float {
            float v = 0.f;
            int64_t flat = 0;

            if (layout == CHW_1x6xN) {
                flat = (int64_t)ch * N + i;
            }
            else {
                flat = i * 6 + ch;
            }

            if (!GetTrigVal_Trt(outFp16, outFp32, flat, v)) return 0.f;
            return v;
            };

        DetCand best0{};
        DetCand best1{};
        float best0_score = -1.f;
        float best1_score = -1.f;

        for (int64_t i = 0; i < N; ++i) {
            float w = get_val(2, i);
            float h = get_val(3, i);
            if (w <= 0.f || h <= 0.f) continue;

            float class_0_score = get_val(4, i);
            float class_1_score = get_val(5, i);

            //Softmax2(class_0_score, class_1_score, class_0_score, class_1_score);

            if (class_0_score > best0_score) {
                best0_score = class_0_score;
                best0.box = cv::Rect2f(get_val(0, i), get_val(1, i), w, h);
                best0.score = class_0_score;
            }
            if (class_1_score > best1_score) {
                best1_score = class_1_score;
                best1.box = cv::Rect2f(get_val(0, i), get_val(1, i), w, h);
                best1.score = class_1_score;
            }
        }

        //for (int64_t i = 0; i < N; ++i) {
        //    float x1 = get_val(0, i);
        //    float y1 = get_val(1, i);
        //    float x2 = get_val(2, i);
        //    float y2 = get_val(3, i);
        //    float w = x2 - x1;
        //    float h = y2 - y1;
        //    if (w <= 0.f || h <= 0.f) continue;

        //    float class_0_score = get_val(4, i);
        //    float class_1_score = get_val(5, i);

        //    if (class_0_score > best0_score) {
        //        best0_score = class_0_score;
        //        best0.box = cv::Rect2f(x1, y1, w, h);
        //        best0.score = class_0_score;
        //    }
        //    if (class_1_score > best1_score) {
        //        best1_score = class_1_score;
        //        best1.box = cv::Rect2f(x1, y1, w, h);
        //        best1.score = class_1_score;
        //    }

        //    //std::cout << "raw xyxy: " << x1 << "," << y1 << "," << x2 << "," << y2 << std::endl;
        //}

        const bool found_tab = (best0_score >= 0.f);
        const bool found_horn = (best1_score >= 0.f);

        if (!found_tab && !found_horn) return;

        if (found_tab) {
            const auto& k = best0;
            cv::Rect2f rf = inv_letterbox_tlxywh_to_frame_rect2f(
                k.box.x, k.box.y, k.box.width, k.box.height, lb);
         /*   cv::Rect2f rf = inv_letterbox_xyxy_to_frame_rect2f(
                k.box.x, k.box.y,
                k.box.x + k.box.width, k.box.y + k.box.height,
                lb);*/

            cv::Rect r = clamp_rect2f_to_int_rect(rf, frameW, frameH);
            if (r.area() > 0) {
                job.trig.tab_score = k.score;
                job.trig.tab = r;
            }
        }

        if (found_horn) {
            const auto& k = best1;

         /*   cv::Rect2f rf = inv_letterbox_xyxy_to_frame_rect2f(
                k.box.x, k.box.y,
                k.box.x + k.box.width, k.box.y + k.box.height,
                lb);*/

            cv::Rect2f rf = inv_letterbox_tlxywh_to_frame_rect2f(
                k.box.x, k.box.y, k.box.width, k.box.height, lb);

            cv::Rect r = clamp_rect2f_to_int_rect(rf, frameW, frameH);
            if (r.area() > 0) {
                job.trig.horn_score = k.score;
                job.trig.horn = r;
            }
        }


        const bool has_tab = job.trig.tab.area() > 0;
        const bool has_horn = job.trig.horn.area() > 0;

        job.trig.ok =
            has_tab && has_horn &&
            (job.trig.tab_score >= tab_min_score) &&
            (job.trig.horn_score >= horn_min_score);
    }

    // ORT 트리거 출력 -> DefectJob 변환
    // - 출력 텐서에서 후보 박스 수집
    // - 클래스별 NMS 적용
    // - letterbox 좌표를 원본 좌표로 변환
    void MapTriggerOutsToJob_Letterbox_Ort(
        const std::vector<Ort::Value>& outs,
        const LetterboxInfo& lb,
        int modelW, int modelH,
        int frameW, int frameH,
        float tab_min_score, float horn_min_score,
        float nms_iou_thr,
        HmCutter::Detector::DefectJob& job)
    {
        job.trig.ok = false;
        job.trig.tab = {};
        job.trig.horn = {};
        job.trig.tab_score = 0.f;
        job.trig.horn_score = 0.f;

        const Ort::Value* det = nullptr;
        ONNXTensorElementDataType elemType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;

        for (const auto& ov : outs) {
            if (!ov.IsTensor()) continue;
            auto ti = ov.GetTensorTypeAndShapeInfo();
            elemType = ti.GetElementType();
            if (elemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
                elemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) continue;

            auto sh = ti.GetShape();
            if (sh.size() == 3 && sh[0] == 1 && sh[1] == 6 && sh[2] > 0) {
                det = &ov;
                break;
            }
        }
        if (!det) return;

        auto ti = det->GetTensorTypeAndShapeInfo();
        auto sh = ti.GetShape();
        const int64_t N = sh[2];
        if (N <= 0) return;

        auto get_val = [&](int ch, int64_t i)->float {
            const int64_t idx = (int64_t)ch * N + i;
            if (elemType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                const float* p = det->GetTensorData<float>();
                return p[idx];
            }
            else {
                const uint16_t* p = reinterpret_cast<const uint16_t*>(
                    det->GetTensorData<Ort::Float16_t>());
                return fp16_to_fp32(p[idx]);
            }
            };

        std::vector<DetCand> cands;
        cands.reserve((size_t)N);

        for (int64_t i = 0; i < N; ++i) {
            float x = get_val(0, i);
            float y = get_val(1, i);
            float w = get_val(2, i);
            float h = get_val(3, i);
            float conf = get_val(4, i);
            float clsf = get_val(5, i);

            if (conf <= 0.f) continue;
            if (w <= 0.f || h <= 0.f) continue;

            int cls = (int)std::llround(clsf);
            cands.push_back({ cv::Rect2f(x, y, w, h), conf, cls });
        }

        if (cands.empty()) return;

        auto kept = NmsPerClass(std::move(cands), nms_iou_thr);

        for (const auto& k : kept) {
            cv::Rect2f rf = inv_letterbox_tlxywh_to_frame_rect2f(
                k.box.x, k.box.y, k.box.width, k.box.height, lb);

            cv::Rect r = clamp_rect2f_to_int_rect(rf, frameW, frameH);
            if (r.area() <= 0) continue;

            if (k.cls == 0) {
                if (k.score >= job.trig.tab_score) {
                    job.trig.tab_score = k.score;
                    job.trig.tab = r;
                }
            }
            else if (k.cls == 1) {
                if (k.score >= job.trig.horn_score) {
                    job.trig.horn_score = k.score;
                    job.trig.horn = r;
                }
            }
        }

        const bool has_tab = job.trig.tab.area() > 0;
        const bool has_horn = job.trig.horn.area() > 0;

        job.trig.ok =
            has_tab && has_horn &&
            (job.trig.tab_score >= tab_min_score) &&
            (job.trig.horn_score >= horn_min_score);
    }

    // 내부 ResultItem -> C API용 ResultItemC 변환
    // - DLL 콜백으로 전달할 때 사용
    ResultItemC ToCResult(const HmCutter::ResultItem& r)
    {
        ResultItemC c{};
        c.defect_type = static_cast<int>(r.defect_type);
        c.score = r.score;
        c.box.x1 = r.box.x1;
        c.box.y1 = r.box.y1;
        c.box.x2 = r.box.x2;
        c.box.y2 = r.box.y2;
        return c;
    }

} // namespace HMSTACK