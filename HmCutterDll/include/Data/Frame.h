#pragma once
#include <cstdint>

#define FRAME_TIMESTAMP_MAX 32

namespace HmCutter {

struct Frame {
    uint64_t index = 0;
    uint8_t* data = nullptr;   // BGR raw pointer
    int width = 0;
    int height = 0;
    char timestamp[FRAME_TIMESTAMP_MAX] = {};  // 타임스탬프 문자열 (예: "20250701132801034")
};

using FrameData = Frame;  // ✔ 동일 개념

} // namespace HmCutter
