#include "DetectorHelpers.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace HMSTACK {

    // FP16(IEEE 754 half) 값을 FP32 float로 변환
    // TRT 출력이 FP16일 때 확률/좌표 복원에 사용됨
    float fp16_to_fp32(uint16_t h)
    {
        uint32_t sign = (h & 0x8000u) << 16;
        uint32_t exp = (h & 0x7C00u) >> 10;
        uint32_t mant = (h & 0x03FFu);

        uint32_t f;
        if (exp == 0) {
            if (mant == 0) {
                f = sign;
            }
            else {
                exp = 1;
                while ((mant & 0x0400u) == 0) { mant <<= 1; exp--; }
                mant &= 0x03FFu;
                uint32_t exp32 = (exp - 1 + 127 - 15) << 23;
                uint32_t mant32 = mant << 13;
                f = sign | exp32 | mant32;
            }
        }
        else if (exp == 31) {
            uint32_t exp32 = 0xFFu << 23;
            uint32_t mant32 = mant ? (mant << 13) : 0;
            f = sign | exp32 | mant32;
        }
        else {
            uint32_t exp32 = (exp + (127 - 15)) << 23;
            uint32_t mant32 = mant << 13;
            f = sign | exp32 | mant32;
        }

        union { uint32_t u; float f; } cvt;
        cvt.u = f;
        return cvt.f;
    }

    // Letterbox 기준 좌표(cx,cy,w,h)를 원본 프레임 좌표계로 되돌리는 함수
   // trigger 결과가 letterbox 공간일 때 ROI 복원에 사용됨
    cv::Rect2f inv_letterbox_tlxywh_to_frame_rect2f(
        float cx, float cy, float w, float h,
        const LetterboxInfo& lb)
    {
        const float r = lb.scale;
        const float padX = lb.pad_left;
        const float padY = lb.pad_top;

        const float x = cx - w * 0.5f;
        const float y = cy - h * 0.5f;

        const float fx = (x - padX) / r;
        const float fy = (y - padY) / r;
        const float fw = w / r;
        const float fh = h / r;

        return cv::Rect2f(fx, fy, fw, fh);
    }

    // Rect2f를 정수 Rect로 변환하고, 프레임 경계로 클램프
  // 좌표가 음수/프레임 밖으로 나가는 상황 방지용
    cv::Rect clamp_rect2f_to_int_rect(const cv::Rect2f& rf, int frameW, int frameH)
    {
        int ix = (int)std::round(rf.x);
        int iy = (int)std::round(rf.y);
        int iw = (int)std::round(rf.width);
        int ih = (int)std::round(rf.height);

        cv::Rect r(ix, iy, std::max(0, iw), std::max(0, ih));
        return r & cv::Rect(0, 0, frameW, frameH);
    }

    // 로그/파일명 용도의 타임스탬프 문자열 생성
  // 형식: "YYYYMMDD_HHMMSS_mmm"
    std::string MakeCreatedTime_YYYYMMDD_HHMMSS_mmm()
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::time_t tt = system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d_%H%M%S")
            << "_" << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    std::vector<int> RectToXYXY(const cv::Rect& r, bool inclusive)
    {
        int x1 = r.x;
        int y1 = r.y;
        int x2 = r.x + r.width;
        int y2 = r.y + r.height;

        if (inclusive) {
            x2 = x2 - 1;
            y2 = y2 - 1;
        }
        return { x1, y1, x2, y2 };
    }

} // namespace HMSTACK