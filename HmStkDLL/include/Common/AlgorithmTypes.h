#pragma once
#include <cstdint>

namespace HMSTACK {

    enum class ErrorCodeEnum : int {
        NO_ERROR = 0,
        FILE_NOT_FOUND = 1,
        MODEL_LOAD_FAILURE = 2,
        INFERENCE_FAILURE = 3,
        INVALID_INPUT = 4,
        UNKNOWN_ERROR = 99
    };

    enum class AlgorithmStatus : int {
        UNLOADED = 0,
        CREATED = 1,
        INITIALIZING = 2,
        READY = 3,
        RUNNING = 4,
        ERROR = 5
    };

    struct ConfThresholds {
        float okQThreshold    = 0.3f;   // Class #1 (OK) Q threshold
        float okAbThreshold   = 0.7f;   // Class #1 (OK) AB threshold
        float ngQThreshold    = 0.4f;   // Class #2 (NG) Q threshold
        float ngAbThreshold   = 0.8f;   // Class #2 (NG) AB threshold
        float etcQThreshold   = 0.25f;  // Class #3 (기타) Q threshold
        float etcAbThreshold  = 0.7f;   // Class #3 (기타) AB threshold
    };

    struct AlgorithmConfig {
        const char* baseDirPath = nullptr;
        ConfThresholds thresholds;
        int useCuda = 1;
        uint32_t struct_size = 0;
    };

    struct LetterboxInfo {
        float scale = 1.0f;
        float pad_left = 0.0f;
        float pad_top = 0.0f;
        float pad_right = 0.0f;
        float pad_bottom = 0.0f;
    };

} // namespace HMSTACK
