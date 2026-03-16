#pragma once
#include <vector>
#include <opencv2/core.hpp>
#include "Include/Common/AlgorithmTypes.h"
#include "Include/Data/ResultItem.h"
#include "Include/Data/ResultItemC.h"
#include "Detector.h"

namespace HmCutter {

    struct DetCand {
        cv::Rect2f box;
        float score;
        int cls; // 0=cell, 1=pnp
    };

    float IoU_TLXYWH(const cv::Rect2f& a, const cv::Rect2f& b);

    std::vector<DetCand> NmsPerClass(std::vector<DetCand> cands, float iouThr);

    bool GetTrigVal_Trt(
        const std::vector<uint16_t>& outFp16,
        const std::vector<float>& outFp32,
        int64_t idx,
        float& v);

    void MapTriggerOutsToJob_Letterbox_Trt(
        const std::vector<uint16_t>& outFp16,
        const std::vector<float>& outFp32,
        const std::vector<int64_t>& outShape,
        const LetterboxInfo& lb,
        int modelW, int modelH,
        int frameW, int frameH,
        float cell_min_score, float pnp_min_score,
        float nms_iou_thr,
        int classStartIndex,
        HmCutter::Detector::DefectJob& job);

    void MapTriggerOutsToJob_Letterbox_Ort(
        const std::vector<Ort::Value>& outs,
        const LetterboxInfo& lb,
        int modelW, int modelH,
        int frameW, int frameH,
        float cell_min_score, float pnp_min_score,
        float nms_iou_thr,
        HmCutter::Detector::DefectJob& job);

    ResultItemC ToCResult(const HmCutter::ResultItem& r);

} // namespace HMSTACK