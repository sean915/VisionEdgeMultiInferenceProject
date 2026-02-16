#pragma once
#pragma once
#include <stdint.h>

#ifdef _WIN32
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT
#endif

extern "C" {

    // ORT 엔진 1회 초기화
    DLL_EXPORT int __cdecl infer_init(
        const wchar_t* onnxPath, // Windows는 wchar_t 경로가 편함
        int useCuda,             // 0/1
        int deviceId,            // 0
        int inputW,              // 640
        int inputH               // 640
    );

    // 종료
    DLL_EXPORT void __cdecl infer_shutdown();

    // (선택) 마지막 에러 메시지
    DLL_EXPORT const char* __cdecl infer_last_error();

}
