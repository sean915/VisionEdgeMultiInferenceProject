#pragma once
#include <cstdio>
#include <cstdarg>

// ✅ <Windows.h> 전체를 include하면 GetVersion, max/min 등 매크로 충돌이 발생하므로
//    OutputDebugStringA만 직접 선언하여 사용
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* lpOutputString);

// ✅ DLL에서 사용하는 로그 함수
// OutputDebugStringA로 출력 → VS 출력창 / DebugView에서 확인 가능
// GUI 앱(WPF 등)에서 LoadLibrary로 로드해도 로그가 보임
inline void DbgLog(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}
