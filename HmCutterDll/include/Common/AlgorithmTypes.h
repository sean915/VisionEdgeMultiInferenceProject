#pragma once
#include <cstdint>

namespace HmCutter {

    enum class ErrorCodeEnum : int {
        NO_ERROR = 0,
        FILE_NOT_FOUND = 1,
        MODEL_LOAD_FAILURE = 2,
        INFERENCE_FAILURE = 3,
        INVALID_INPUT = 4,
        UNKNOWN_ERROR = 99
    };

    // ✅ AlgorithmStatus: 실행 중 여부(bool) + 상태 변경 시각(epoch ms)
    struct AlgorithmStatusInfo {
        bool    is_running   = false;  // true=RUNNING, false=not running
        int64_t timestamp_ms = 0;      // 상태가 마지막으로 변경된 시각 (epoch ms)
    };

    // ✅ C API 반환용 plain-C 레이아웃 (bool 대신 int8_t 사용)
    struct AlgorithmStatusC {
        int8_t  is_running;   // 1=running, 0=not running
        int64_t timestamp_ms; // epoch ms
    };

    // ✅ 프레임 큐 디버깅 통계 (C API용)
    struct FrameQueueStatsC {
        uint64_t push_count;         // pushFrame 호출 성공 횟수
        uint64_t pop_count;          // triggerLoop에서 pop한 횟수
        uint64_t drop_count;         // 큐 오버플로로 드랍된 프레임 수
        uint64_t queue_size;         // 현재 큐에 남은 항목 수
        uint64_t last_pushed_index;  // 마지막으로 push된 프레임 인덱스
        uint64_t last_popped_index;  // 마지막으로 pop된 프레임 인덱스
    };

    struct ConfThresholds {
        double okQThreshold    = 0.3;   // Class #1 (OK) Q threshold
        double okAbThreshold   = 0.7;   // Class #1 (OK) AB threshold
        double ngQThreshold    = 0.3;   // Class #2 (NG) Q threshold
        double ngAbThreshold   = 0.7;   // Class #2 (NG) AB threshold
        double etcQThreshold   = 0.3;   // Class #3 (기타) Q threshold
        double etcAbThreshold  = 0.7;   // Class #3 (기타) AB threshold
    };

    struct ModelPaths {
        const char* triggerEnginePath = nullptr; // trigger 모델 전체 경로 (예: "C:\models\Trigger_V1.engine")
        const char* defectEnginePath  = nullptr; // defect  모델 전체 경로 (예: "C:\models\Defect_V1.engine")
    };

    // ✅ Input ROI (x1,y1,x2,y2) — 0,0,0,0이면 ROI 미적용(전체 프레임 사용)
    struct InputRoi {
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
    };

    struct AlgorithmConfig {
        ModelPaths     paths;
        ConfThresholds thresholds;
        InputRoi       roi;
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

} // namespace HmCutter
