#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include "include/Common/AlgorithmTypes.h"

namespace HMSTACK {

    float fp16_to_fp32(uint16_t h);

    cv::Rect2f inv_letterbox_tlxywh_to_frame_rect2f(
        float cx, float cy, float w, float h,
        const LetterboxInfo& lb);

    cv::Rect clamp_rect2f_to_int_rect(const cv::Rect2f& rf, int frameW, int frameH);

    std::string MakeCreatedTime_YYYYMMDD_HHMMSS_mmm();

    std::vector<int> RectToXYXY(const cv::Rect& r, bool inclusive);

} // namespace HMSTACK