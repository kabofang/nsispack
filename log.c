#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdlib.h>

void XNSIS_LogW(const char* file, const char* func, int line, const wchar_t* fmt, ...) {
  wchar_t wfile[128] = { 0 }, wfunc[128] = { 0 };
  size_t converted = 0;
  mbstowcs_s(&converted, wfile, 128, file, _TRUNCATE);
  mbstowcs_s(&converted, wfunc, 128, func, _TRUNCATE);
  wprintf(L"[XNSIS][%s][%s][%d] ", wfile, wfunc, line);
  va_list args;
  va_start(args, fmt);
  vwprintf(fmt, args);
  va_end(args);
  wprintf(L"\n");
}
