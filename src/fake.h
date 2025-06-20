#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

int Main2CustomNoExcept(int numArgs, char* args[]);
int Extract7z(LPTSTR archive, LPTSTR dir, HWND, int progress_type);

#ifdef __cplusplus
}
#endif