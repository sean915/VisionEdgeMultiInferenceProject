#pragma once
#include <cstdint>

struct BoxC {
    int x1, y1, x2, y2;
};

struct ResultItemC {
    int defect_type;
    float score;
    BoxC box;
};

