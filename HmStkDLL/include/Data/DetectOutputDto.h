#pragma once
#include <memory>
#include <vector>
#include <string>
#include <opencv2/core.hpp>
#include "Data/Frame.h"
#include "Data/ResultItem.h"

namespace HMSTACK {

struct DetectOutputDto {
    std::shared_ptr<Frame> frame;
    cv::Mat visualized_img;
    uint32_t takt_time = 0;
    std::vector<ResultItem> ab_results; // 이상 결과
    std::vector<ResultItem> q_results;  // 의심 결과
    std::vector<ResultItem> normal_results;  // 
    int error_code = 0;
    std::string error_msg;
};

struct DetectResultDto {
    Box box;
    int label;
    float score;
    int size;
    cv::Mat mask;
    cv::Mat heatmap;

    ~DetectResultDto() {
        if (!mask.empty()) {
            mask.release();
        }
        if (!heatmap.empty()) {
            heatmap.release();
        }
    }
};

} // namespace HMSTACK
