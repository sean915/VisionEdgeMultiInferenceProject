#pragma once
#include <opencv2/core.hpp>
#include "Include/Common/AlgorithmTypes.h"

namespace HMSTACK {

    cv::Mat letterbox_bgr(
        const cv::Mat& srcBgr,
        int dstW,
        int dstH,
        LetterboxInfo& lb,
        const cv::Scalar& padColor = cv::Scalar(114, 114, 114));

} // namespace HMSTACK