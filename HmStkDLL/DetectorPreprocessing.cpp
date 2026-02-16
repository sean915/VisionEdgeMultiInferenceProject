#include "DetectorPreprocessing.h"
#include <opencv2/imgproc.hpp>

namespace HMSTACK {

    // 입력 이미지를 letterbox 방식으로 리사이즈 + 패딩
    // - 종횡비를 유지하면서 dstW/dstH에 맞춤
    // - 패딩 정보(lb)는 후처리에서 원본 좌표 복원에 사용됨
    cv::Mat letterbox_bgr(
        const cv::Mat& srcBgr,
        int dstW, int dstH,
        LetterboxInfo& lb,
        const cv::Scalar& padColor)
    {
        const int srcW = srcBgr.cols;
        const int srcH = srcBgr.rows;

        // 종횡비 유지 비율 계산
        const float r = std::min(dstW / (float)srcW, dstH / (float)srcH);
        const int newW = (int)std::round(srcW * r);
        const int newH = (int)std::round(srcH * r);

        // 남는 영역(패딩) 계산
        const int padW = dstW - newW;
        const int padH = dstH - newH;

        const int padL = padW / 2;
        const int padT = padH / 2;
        const int padR = padW - padL;
        const int padB = padH - padT;

        // 후처리용 letterbox 정보 저장
        lb.scale = r;
        lb.pad_left = (float)padL;
        lb.pad_top = (float)padT;
        lb.pad_right = (float)padR;
        lb.pad_bottom = (float)padB;

        // 리사이즈
        cv::Mat resized;
        cv::resize(srcBgr, resized, cv::Size(newW, newH), 0, 0, cv::INTER_LINEAR);

        // 패딩 추가
        cv::Mat out;
        cv::copyMakeBorder(
            resized,
            out,
            padT, padB,
            padL, padR,
            cv::BORDER_CONSTANT,
            padColor
        );

        return out;
    }

} // namespace HMSTACK