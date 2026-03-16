#pragma once
#include <opencv2/core.hpp>
#include "Include/Common/AlgorithmTypes.h"

namespace HmCutter {

    cv::Mat letterbox_bgr(
        const cv::Mat& srcBgr,
        int dstW,
        int dstH,
        LetterboxInfo& lb,
        const cv::Scalar& padColor = cv::Scalar(114, 114, 114));

    // ✅ 버퍼 재사용 오버로드: 매 프레임 힙 할당 제거
    void letterbox_bgr(
        const cv::Mat& srcBgr,
        int dstW,
        int dstH,
        LetterboxInfo& lb,
        cv::Mat& resizedBuf,
        cv::Mat& outBuf,
        const cv::Scalar& padColor = cv::Scalar(114, 114, 114));

} // namespace HmCutter