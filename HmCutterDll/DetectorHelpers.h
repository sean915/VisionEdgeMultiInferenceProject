#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include "Include/Common/AlgorithmTypes.h"

namespace HmCutter {

    float fp16_to_fp32(uint16_t h);

    cv::Rect2f inv_letterbox_tlxywh_to_frame_rect2f(
        float cx, float cy, float w, float h,
        const LetterboxInfo& lb);

    cv::Rect clamp_rect2f_to_int_rect(const cv::Rect2f& rf, int frameW, int frameH);

    std::string MakeCreatedTime_YYYYMMDD_HHMMSS_mmm();
    cv::Rect2f inv_letterbox_xyxy_to_frame_rect2f(
        float x1, float y1, float x2, float y2,
        const LetterboxInfo& lb);
    std::vector<int> RectToXYXY(const cv::Rect& r, bool inclusive);

} // namespace HmCutter