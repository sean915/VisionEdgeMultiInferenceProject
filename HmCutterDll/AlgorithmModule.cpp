#include "Include/Common/AD_API.h"
#include "Include/Data/ResultItemC.h"
#include "Detector.h"
#include "Include/Utills/DebugLog.h"
#include <vector>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

using namespace HmCutter;

extern "C" {

    constexpr const char* kAlgorithmVersion = "2.0.0";

    AD_API void* CreateAlgorithm(const AlgorithmConfig* config) {
        if (!config) return nullptr;
        return new Detector(*config);
    }

    AD_API int Initialize(void* handle) {
        if (!handle) return -1;
        auto* det = static_cast<Detector*>(handle);
        det->model_setup();

        // model_setup 실패 시 세션이 null → 에러 반환
        bool hasSession = (det->trigger_sess_trt != nullptr && det->defect_sess_trt != nullptr)
                       || (det->trigger_sess_ != nullptr && det->defect_sess_ != nullptr);
        if (!hasSession) {
            DbgLog("[Initialize] FAILED: model sessions not loaded.\n");
            return -2;
        }
        return 0;
    }

    AD_API int Run(void* handle) {
        if (!handle) return -1;
        auto* det = static_cast<Detector*>(handle);
        det->run();

        // triggerLoop이 status_.is_running = true 세팅할 시간 확보
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // run()이 실제로 스레드를 시작했는지 확인
        auto status = det->getStatus();
        if (!status.is_running) {
            DbgLog("[Run] FAILED: detector is not running after run() call.\n");
            return -2;
        }
        return 0;
    }

    AD_API int Stop(void* handle) {
        if (!handle) return -1;
        static_cast<Detector*>(handle)->stop();
        return 0;
    }

    // ==================== PLC 설비 상태 ====================
    // 호출할 때마다 0↔1 토글. DLL handle이 살아있으면 토글된 값을 반환.
    // 반환값: 0 또는 1 (토글된 상태), -1=handle 없음
    AD_API int NotifyEquipStatus(void* handle, int equip_status) {
        if (!handle) return -1;

        auto* det = static_cast<Detector*>(handle);
        return det->notifyEquipStatus();
    }

    AD_API int PushFrame(void* handle, const FrameData* frame) {
        if (!handle || !frame) return -1;
        return static_cast<Detector*>(handle)->pushFrame(*frame);
    }
    // ==================== Callback ====================
    // C# P/Invoke 호환 콜백 시그니처
    typedef void (*ResultCallbackFunc)(
        uint64_t frameIndex,
        const ResultItemC* results,
        int errorCode,
        const char* errorMsg,
        const ImageDataC* visImg,     // 시각화된 이미지 (bbox/label 오버레이)
        const ImageDataC* rawImg      // 시각화되지 않은 원본 이미지
        );


    AD_API void SetResultCallback(void* handle, ResultCallbackFunc callback) {
        if (!handle) return;
        static_cast<Detector*>(handle)->setCResultCallback(callback);
    }

    // GetStatus: 실행 중 여부(is_running) + 상태 변경 시각(timestamp_ms)를 반환합니다.
    // out_status가 nullptr이 아니면 구조체를 채웁니다.
    // 반환값: 1=RUNNING, 0=not running, -1=handle 없음
    AD_API int GetStatus(void* handle, AlgorithmStatusC* out_status) {
        if (!handle) return -1;
        const auto s = static_cast<Detector*>(handle)->getStatus();
        if (out_status) {
            out_status->is_running   = s.is_running ? 1 : 0;
            out_status->timestamp_ms = s.timestamp_ms;
        }
        return s.is_running ? 1 : 0;
    }

    AD_API const char* GetVersion() {
        return kAlgorithmVersion;
    }

    AD_API int GetFrameStats(void* handle, FrameQueueStatsC* out_stats) {
        if (!handle || !out_stats) return -1;
        *out_stats = static_cast<Detector*>(handle)->getFrameStats();
        return 0;
    }

    AD_API void Destroy(void* handle) {
        if (!handle) return;
        delete static_cast<Detector*>(handle);
    }


} // extern "C"
