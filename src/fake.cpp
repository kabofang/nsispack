#include "fake.h"

int Main2CustomNoExcept(int numArgs, char* args[]) {
  int ret = _wsystem((wchar_t*)args);
  return ret;
}

int Extract7z(LPTSTR archive, LPTSTR dir, HWND, int progress_type) {
  // 直接拼接 "7z " + 用户命令，并调用系统执行
  wchar_t cmd[1024];
  wsprintfW(cmd, L"7z x \"%s\" -o\"%s\" -aos", archive, dir);
  int ret = _wsystem(cmd);

  // 返回是否成功（ret == 0 表示成功）
  return ret;
}