#pragma once
#include "Include/Data/DetectOutputDto.h"

namespace HmCutter {

class IResultListener {
public:
    virtual ~IResultListener() = default;

    virtual bool OnResultDetected(
        const Frame& frame,
        const DetectOutputDto& output
    ) = 0;
};

} // namespace HMSTACK
