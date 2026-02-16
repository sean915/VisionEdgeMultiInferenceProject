#pragma once
#include <memory>
#include <string>

namespace HMSTACK {

class IDetector {
public:
    virtual ~IDetector() = default;

    virtual void model_setup() = 0;
     virtual void updateConfig(const std::string& key,
                              const std::string& value) = 0;
};

} // namespace HMSTACK
