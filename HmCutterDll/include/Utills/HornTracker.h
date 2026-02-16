#pragma once
#include <vector>
#include "Data/ResultItem.h"

namespace HMSTACK {

class HornTracker {
public:
    bool update(const std::vector<ResultItem>&) {
        return false; // Stub: 항상 트리거 안 함
    }
};

} // namespace HMSTACK
