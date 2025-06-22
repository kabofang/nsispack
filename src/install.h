#pragma once
#include <windows.h>
#include "distinfo.h"
#ifdef __cplusplus
extern "C" {
#endif

  typedef struct {
    InstallDistInfo distinfo;
    wchar_t temp_dir[MAX_PATH];
    wchar_t install7z_path[MAX_PATH];
    wchar_t** real_dirs;
    DWORD real_dir_count;
    DWORD real_dirs_capacity;
    HWND hwnd;
  } InstallContext;

int InstallContext_Init(InstallContext* ctx, const wchar_t* distinfo_path);
void InstallContext_Free(InstallContext* ctx);
int SetCurrentRealOutDir(InstallContext* ctx, const wchar_t* real_dir);
int ExtractInstall7z(InstallContext* ctx, const wchar_t* install7z_path);

#ifdef __cplusplus
}
#endif 