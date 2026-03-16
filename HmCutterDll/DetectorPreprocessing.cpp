#include "DetectorPreprocessing.h"
#include <opencv2/imgproc.hpp>

namespace HmCutter {

    // 기존 API 유지
    cv::Mat letterbox_bgr(
        const cv::Mat& srcBgr,
        int dstW, int dstH,
        LetterboxInfo& lb,
        const cv::Scalar& padColor)
    {
        cv::Mat resized, out;
        letterbox_bgr(srcBgr, dstW, dstH, lb, resized, out, padColor);
        return out;
    }

    // ✅ 버퍼 재사용 오버로드
    void letterbox_bgr(
        const cv::Mat& srcBgr,
        int dstW, int dstH,
        LetterboxInfo& lb,
        cv::Mat& resizedBuf,
        cv::Mat& outBuf,
        const cv::Scalar& padColor)
    {
        const int srcW = srcBgr.cols;
        const int srcH = srcBgr.rows;

        const float r = std::min(dstW / (float)srcW, dstH / (float)srcH);
        const int newW = (int)std::round(srcW * r);
        const int newH = (int)std::round(srcH * r);

        const int padW = dstW - newW;
        const int padH = dstH - newH;

        const int padL = padW / 2;
        const int padT = padH / 2;
        const int padR = padW - padL;
        const int padB = padH - padT;

        lb.scale = r;
        lb.pad_left = (float)padL;
        lb.pad_top = (float)padT;
        lb.pad_right = (float)padR;
        lb.pad_bottom = (float)padB;

        cv::resize(srcBgr, resizedBuf, cv::Size(newW, newH), 0, 0, cv::INTER_LINEAR);

        if (padW == 0 && padH == 0) {
            outBuf = resizedBuf;
            return;
        }

        cv::copyMakeBorder(
            resizedBuf, outBuf,
            padT, padB, padL, padR,
            cv::BORDER_CONSTANT, padColor
        );
    }

} // namespace HmCutter