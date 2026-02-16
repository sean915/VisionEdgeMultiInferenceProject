#pragma once
#include <stdint.h>

#ifdef _WIN32
#define RTSP_API __declspec(dllexport)
#define RTSP_CALL __cdecl
#else
#define RTSP_API
#define RTSP_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum RtspLogLevel {
        RTSP_LOG_INFO = 1,
        RTSP_LOG_WARN = 2,
        RTSP_LOG_ERROR = 3
    } RtspLogLevel;

    // ✅ (기존) BGRA 고정 콜백
    typedef void (RTSP_CALL* RtspFrameCallback)(
        void* user,
        int channelId,
        const uint8_t* bgra,
        int width,
        int height,
        int stride,
        int64_t pts_ms
        );

    // ✅ (신규) 픽셀 포맷
    typedef enum RtspPixelFormat {
        RTSP_PIXFMT_BGRA32 = 0,
        RTSP_PIXFMT_BGR24 = 1
    } RtspPixelFormat;

    // ✅ (신규) 픽셀 포맷 포함 콜백
    typedef void (RTSP_CALL* RtspFrameCallbackEx)(
        void* user,
        int channelId,
        const uint8_t* data,
        int width,
        int height,
        int stride,
        int64_t pts_ms,
        RtspPixelFormat pixfmt
        );

    typedef void (RTSP_CALL* RtspStatusCallback)(
        void* user,
        int channelId,
        RtspLogLevel level,
        int code,
        const char* message
        );

    typedef void* RtspHandle;

    // -------------------- 기존 Options/API 유지 --------------------
    typedef struct RtspOptions {
        int tcp_only;
        int stimeout_us;
        int reconnect;
        int reconnect_wait_ms;
        int target_fps; // 0: 원본 / 10: 10fps 스로틀
    } RtspOptions;

    RTSP_API void RTSP_CALL rtsp_default_options(RtspOptions* opt);
    RTSP_API RtspHandle RTSP_CALL rtsp_create(
        int channelId,
        const char* rtspUrl,
        const RtspOptions* opt,
        RtspFrameCallback onFrame,
        RtspStatusCallback onStatus,
        void* user
    );

    // -------------------- ✅ 신규 Options2/API 추가 --------------------
    typedef struct RtspOptions2 {
        uint32_t struct_size;       // ✅ ABI 안전장치
        int tcp_only;
        int stimeout_us;
        int reconnect;
        int reconnect_wait_ms;
        int target_fps;
        RtspPixelFormat out_pixfmt; // ✅ BGRA32 or BGR24
    } RtspOptions2;

    RTSP_API void RTSP_CALL rtsp_default_options2(RtspOptions2* opt);

    RTSP_API RtspHandle RTSP_CALL rtsp_create2(
        int channelId,
        const char* rtspUrl,
        const RtspOptions2* opt,
        RtspFrameCallbackEx onFrameEx,
        RtspStatusCallback onStatus,
        void* user
    );

    RTSP_API int  RTSP_CALL rtsp_start(RtspHandle h);
    RTSP_API void RTSP_CALL rtsp_stop(RtspHandle h);
    RTSP_API void RTSP_CALL rtsp_destroy(RtspHandle h);
    RTSP_API int RTSP_CALL rtsp_request_clip(RtspHandle h, int64_t center_pts_ms, int pre_ms, int post_ms, const char* out_path);

#ifdef __cplusplus
}
#endif
