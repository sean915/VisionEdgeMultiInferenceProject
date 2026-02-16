#pragma once
#include <cstdint>

namespace HMSTACK {

struct Frame {
    uint64_t index = 0;
    uint8_t* data = nullptr;   // BGR raw pointer
    int width = 0;
    int height = 0;
    int64_t timestamp = 0;
};

using FrameData = Frame;  // ✔ 동일 개념

} // namespace HMSTACK
