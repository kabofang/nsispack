#pragma once
#include <wchar.h>
#include <Windows.h>
#ifdef __cplusplus
extern "C" {
#endif

void XNSIS_LogW(const char* file, const char* func, int line, const wchar_t* fmt, ...);
#define XNSIS_LOG(msg, ...) XNSIS_LogW(__FILE__, __FUNCTION__, __LINE__, msg, ##__VA_ARGS__)
#ifdef __cplusplus
}
#endif 