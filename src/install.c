#include "install.h"
#include "log.h"
#include <wchar.h>
#include <stdio.h>
#ifdef DBG_SOLUTION
#include"fake.h"
#else
#include "../../../Contrib/7-Zip/Contrib/nsis7z/CPP/7zip/UI/Console/Console7zMain.h"
#include "../../../Contrib/7-Zip/Contrib/nsis7z/CPP/7zip/UI/NSIS/Extract7z.h"
#endif

// 递归创建目录
static int CreateDirRecursiveW(const wchar_t* path) {
  if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
    return 1;
  wchar_t parent[MAX_PATH];
  wcsncpy_s(parent, MAX_PATH, path, _TRUNCATE);
  wchar_t* last = wcsrchr(parent, L'\\');
  if (last) {
    *last = 0;
    if (!CreateDirRecursiveW(parent)) {
      XNSIS_LOG(L"Failed to create parent directory: %s", parent);
      return 0;
    }
  }
  if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
    XNSIS_LOG(L"CreateDirectoryW failed: %s, error=%lu", path, GetLastError());
    return 0;
  }
  return 1;
}

// 安全递归删除目录，防止误删根目录
static int DeleteDirRecursiveW(const wchar_t* path) {
  if (!path || !*path || wcslen(path) < 4) {
    XNSIS_LOG(L"Path too short or empty: %s", path ? path : L"NULL");
    return 0;
  }
  wchar_t search[MAX_PATH];
  wsprintfW(search, L"%s\\*", path);
  WIN32_FIND_DATAW findData;
  HANDLE hFind = FindFirstFileW(search, &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    XNSIS_LOG(L"FindFirstFileW failed: %s, error=%lu", path, GetLastError());
    return 0;
  }
  do {
    if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
    wchar_t sub[MAX_PATH];
    wsprintfW(sub, L"%s\\%s", path, findData.cFileName);
    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!DeleteDirRecursiveW(sub)) {
        XNSIS_LOG(L"Failed to delete subdir: %s", sub);
        continue;
      }
    }
    else {
      if (!DeleteFileW(sub)) {
        XNSIS_LOG(L"Failed to delete file: %s, error=%lu", sub, GetLastError());
        continue;
      }
    }
  } while (FindNextFileW(hFind, &findData));
  FindClose(hFind);
  WIN32_FIND_DATAW findData2;
  HANDLE hFind2 = FindFirstFileW(search, &findData2);
  int empty = 1;
  if (hFind2 != INVALID_HANDLE_VALUE) {
    do {
      if (wcscmp(findData2.cFileName, L".") && wcscmp(findData2.cFileName, L"..")) {
        empty = 0; break;
      }
    } while (FindNextFileW(hFind2, &findData2));
    FindClose(hFind2);
  }
  if (empty) {
    if (!RemoveDirectoryW(path)) {
      XNSIS_LOG(L"Failed to remove directory: %s, error=%lu", path, GetLastError());
      return 0;
    }
  }
  return 1;
}

// 初始化InstallContext（只加载distinfo，不分配real_dirs）
int InstallContext_Init(InstallContext* ctx, const wchar_t* distinfo_path) {
  if (!ctx) return 0;
  memset(&ctx->distinfo, 0, sizeof(ctx->distinfo));
  int result = DistInfo_Load(&ctx->distinfo, distinfo_path);
  DeleteFileW(distinfo_path);
  return result;
}

// 释放InstallContext
void InstallContext_Free(InstallContext* ctx) {
  DistInfo_Free(&ctx->distinfo);
  if (ctx->real_dirs) {
    for (DWORD i = 0; i < ctx->real_dir_count; ++i) free(ctx->real_dirs[i]);
    free(ctx->real_dirs);
  }
  if (!DeleteDirRecursiveW(ctx->temp_dir)) {
    XNSIS_LOG(L"Failed to delete temp_dir: %s", ctx->temp_dir);
  }
}

// 先收集real_dirs
int SetCurrentRealOutDir(InstallContext* ctx, const wchar_t* real_dir) {
  if (!ctx || !real_dir) return 0;
  if (ctx->real_dir_count == ctx->real_dirs_capacity) {
    DWORD new_cap = ctx->real_dirs_capacity ? ctx->real_dirs_capacity * 2 : 8;
    wchar_t** new_dirs = (wchar_t**)realloc(ctx->real_dirs, new_cap * sizeof(wchar_t*));
    if (!new_dirs) return 0;
    ctx->real_dirs = new_dirs;
    ctx->real_dirs_capacity = new_cap;
  }
  size_t len = wcslen(real_dir);
  ctx->real_dirs[ctx->real_dir_count] = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
  if (!ctx->real_dirs[ctx->real_dir_count]) return 0;
  wcsncpy_s(ctx->real_dirs[ctx->real_dir_count], len + 1, real_dir, len);
  ctx->real_dir_count++;
  return 1;
}
// 解压install.7z到临时目录并分发所有文件（在多次SetCurrentRealOutDir之后调用）
int ExtractInstall7z(InstallContext* ctx) {
  if (!ctx) {
    XNSIS_LOG(L"Invalid parameters");
    return 0;
  }

  wcsncpy_s(ctx->install7z_path, MAX_PATH, ctx->distinfo.install7z_name, _TRUNCATE);

  // 创建临时目录
  GetTempPathW(MAX_PATH, ctx->temp_dir);
  wcscat_s(ctx->temp_dir, MAX_PATH, L"install_tmp");
  if (!CreateDirectoryW(ctx->temp_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
    XNSIS_LOG(L"CreateDirectoryW for temp_dir failed: %s, error=%lu", ctx->temp_dir, GetLastError());
    return 0;
  }
  
  // 解压install.7z到临时目录
  wchar_t cmd[1024];
  wsprintfW(cmd, L"7z x \"%s\" -o\"%s\" -aos", ctx->install7z_path, ctx->temp_dir);
  if (Extract7z(ctx->install7z_path, ctx->temp_dir, ctx->hwnd, 1)) {
    XNSIS_LOG(L"SyncCall7zSync failed for: %s", cmd);
    return 0;
  }
  
  // 处理InstallPlugin信息：重新压缩.nsisbin目录
  for (DWORD i = 0; i < ctx->distinfo.plugin_count; ++i) {
    InstallPlugin* plugin = &ctx->distinfo.plugins[i];
    
    // 构建.nsisbin目录路径
    wchar_t nsisbin_dir[MAX_PATH];
    wsprintfW(nsisbin_dir, L"%s\\%s.nsisbin", ctx->temp_dir, plugin->path);
    
    // 检查.nsisbin目录是否存在
    if (GetFileAttributesW(nsisbin_dir) == INVALID_FILE_ATTRIBUTES) {
      XNSIS_LOG(L"nsisbin directory not found: %s", nsisbin_dir);
      continue;
    }
    
    // 构建原始文件路径
    wchar_t original_file[MAX_PATH];
    wsprintfW(original_file, L"%s\\%s", ctx->temp_dir, plugin->path);
    
    // 使用插件指定的压缩参数重新压缩
    wchar_t compress_cmd[2048];
    wsprintfW(compress_cmd, L"7z a %s \"%s\" \"%s\\*\"", plugin->compress_param, original_file, nsisbin_dir);
    
    if (Main2CustomNoExcept(1, (char**)compress_cmd)) {
      XNSIS_LOG(L"Failed to recompress plugin: %s", plugin->path);
      return 0;
    }
    
    XNSIS_LOG(L"Successfully recompressed plugin: %s", plugin->path);
  }
  
  // 分发所有fake目录下的文件到对应的真实目录
  for (DWORD i = 0; i < min(ctx->real_dir_count, ctx->distinfo.dir_count); ++i) {
    InstallFakeDir* fdir = &ctx->distinfo.dirs[i];
    wchar_t* real_dir = ctx->real_dirs[i];
    
    for (DWORD j = 0; j < fdir->file_count; ++j) {
      wchar_t src[MAX_PATH], dst[MAX_PATH];
      wsprintfW(src, L"%s\\%s", ctx->temp_dir, fdir->file_list[j]);
      wsprintfW(dst, L"%s\\%s", real_dir, fdir->file_list[j]);
      wchar_t* last = wcsrchr(dst, L'\\');
      if (last) {
        *last = 0;
        if (!CreateDirRecursiveW(dst)) {
          XNSIS_LOG(L"Failed to create dir: %s", dst);
          return 0;
        }
        *last = L'\\';
      }
      if (!CopyFileW(src, dst, FALSE)) {
        XNSIS_LOG(L"Failed to copy file: %s -> %s, error=%lu", src, dst, GetLastError());
        return 0;
      }
    }
  }
  DeleteFileW(ctx->install7z_path);
  return 1;
}

// 获取install.7z文件名（从distinfo中解析）
const wchar_t* GetInstall7zName(InstallContext* ctx) {
  if (!ctx) {
    XNSIS_LOG(L"Invalid context");
    return NULL;
  }
  return ctx->distinfo.install7z_name;
}