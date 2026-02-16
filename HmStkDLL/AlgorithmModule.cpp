#include "Include/Common/AD_API.h"
#include "Include/Data/ResultItemC.h"
#include "Detector.h"
#include <vector>
#include <cstring>

using namespace HMSTACK;

extern "C" {

    constexpr const char* kAlgorithmVersion = "2.0.0";

    AD_API void* CreateAlgorithm(const AlgorithmConfig* config) {
        if (!config) return nullptr;
        return new Detector(*config);
    }

    AD_API int Initialize(void* handle) {
        if (!handle) return -1;
        static_cast<Detector*>(handle)->model_setup();
        return 0;
    }

    AD_API int Run(void* handle) {
        if (!handle) return -1;
        static_cast<Detector*>(handle)->run();
        return 0;
    }

    AD_API int Stop(void* handle) {
        if (!handle) return -1;
        static_cast<Detector*>(handle)->stop();
        return 0;
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
        const uint8_t* imgData,
        int imgW,
        int imgH,
        int imgStrideBytes,
        int imgCvType
        );


    AD_API void SetResultCallback(void* handle, ResultCallbackFunc callback) {
        if (!handle) return;
        static_cast<Detector*>(handle)->setCResultCallback(callback);
    }

    AD_API int GetStatus(void* handle) {
        if (!handle) return static_cast<int>(AlgorithmStatus::ERROR);
        return static_cast<int>(static_cast<Detector*>(handle)->getStatus());
    }

    AD_API const char* GetVersion() {
        return kAlgorithmVersion;
    }

    AD_API void Destroy(void* handle) {
        if (!handle) return;
        delete static_cast<Detector*>(handle);
    }



} // extern "C"
