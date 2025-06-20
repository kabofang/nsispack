#include "pack.h"
#include <string>
#include <windows.h>
#include <cstdio>
#include <ctime>
#include <set>
#include <functional>
#include <wchar.h>
#include <stdio.h>
#include <cwchar>
#include <cstdlib>
#include "log.h"
#include "tchar.h"

#ifdef DBG_SOLUTION
#include"fake.h"
#else
#include "../build.h"
#endif

// 递归遍历目录下所有文件，回调绝对路径和相对路径
void ForEachFileRecursive(const std::wstring& dir_path, const std::wstring& base_path,
  std::function<void(const std::wstring& abs, const std::wstring& rel)> cb) {
  WIN32_FIND_DATAW findData;
  std::wstring search = dir_path + L"\\*";
  HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    XNSIS_LOG(L"FindFirstFileW failed: %s, error=%lu", search.c_str(), GetLastError());
    return;
  }
  do {
    if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
    std::wstring abs = dir_path + L"\\" + findData.cFileName;
    std::wstring rel = abs.substr(base_path.size() + 1); // +1 for '\'
    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      ForEachFileRecursive(abs, base_path, cb);
    }
    else {
      cb(abs, rel);
    }
  } while (FindNextFileW(hFind, &findData));
  FindClose(hFind);
}

bool IsDirExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool CreateDirRecursive(const std::wstring& path) {
  if (IsDirExists(path)) return true;
  size_t pos = path.find_last_of(L"\\/");
  if (pos != std::wstring::npos) {
    if (!CreateDirRecursive(path.substr(0, pos))) {
      XNSIS_LOG(L"Failed to create parent directory: %s", path.substr(0, pos).c_str());
      return false;
    }
  }
  if (!CreateDirectoryW(path.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
    XNSIS_LOG(L"CreateDirectoryW failed: %s, error=%lu", path.c_str(), GetLastError());
    return false;
  }
  return true;
}

// 安全递归删除目录，防止误删根目录
static int DeleteDirRecursiveW(const std::wstring& path) {
  if (path.empty() || path.size() < 4) {
    XNSIS_LOG(L"Path too short or empty: %s", path.c_str());
    return 0;
  }
  WIN32_FIND_DATAW findData;
  std::wstring search = path + L"\\*";
  HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    XNSIS_LOG(L"FindFirstFileW failed: %s, error=%lu", search.c_str(), GetLastError());
    return 0;
  }
  do {
    if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
    std::wstring sub = path + L"\\" + findData.cFileName;
    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!DeleteDirRecursiveW(sub)) {
        XNSIS_LOG(L"Failed to delete subdir: %s", sub.c_str());
        continue;
      }
    }
    else {
      if (!DeleteFileW(sub.c_str())) {
        XNSIS_LOG(L"Failed to delete file: %s, error=%lu", sub.c_str(), GetLastError());
        continue;
      }
    }
  } while (FindNextFileW(hFind, &findData));
  FindClose(hFind);
  WIN32_FIND_DATAW findData2;
  HANDLE hFind2 = FindFirstFileW(search.c_str(), &findData2);
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
    if (!RemoveDirectoryW(path.c_str())) {
      XNSIS_LOG(L"Failed to remove directory: %s, error=%lu", path.c_str(), GetLastError());
      return 0;
    }
  }
  return 1;
}

bool PackInstall::SyncCall7zSync(const std::wstring& szCommand) {
  XNSIS_LOG(_T("XNSIS: 7z cmd, %s"), szCommand.c_str());
  STARTUPINFOW si = { sizeof(STARTUPINFOW) };
  PROCESS_INFORMATION pi = { 0 };
  BOOL bSuccess = FALSE;
  DWORD dwExitCode = 0;
#ifdef DBG_SOLUTION
  std::wstring sz7zPath = L"7z";
#else
  std::wstring sz7zPath = GetCurrentModuleDir() + L"7z.exe";
#endif
  std::wstring cmdLine = sz7zPath + L" " + szCommand;

  bSuccess = ::CreateProcessW(
    NULL,
    const_cast<LPWSTR>(cmdLine.c_str()),
    NULL,
    NULL,
    FALSE,
    0,
    NULL,
    NULL,
    &si,
    &pi
  );

  if (!bSuccess) {
    XNSIS_LOG(_T("XNSIS: CreateProcess failed, %d"), GetLastError());
    return false;
  }

  ::WaitForSingleObject(pi.hProcess, INFINITE);
  ::GetExitCodeProcess(pi.hProcess, &dwExitCode);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
  if (dwExitCode != 0) {
    XNSIS_LOG(_T("XNSIS: 7z cmd failed"));
  }

  return (dwExitCode == 0);
}

std::wstring PackInstall::GetCurrentModuleDir() {
  wchar_t szPath[MAX_PATH] = { 0 };
  GetModuleFileNameW(NULL, szPath, MAX_PATH);

  std::wstring path(szPath);
  size_t lastSlash = path.find_last_of(L"\\/");
  if (lastSlash != std::wstring::npos) {
    return path.substr(0, lastSlash + 1);
  }
  return L".\\";
}

PackInstall::PackInstall() {
  InitTempDir();
  ParseConfigIni();
}

bool PackInstall::InitTempDir() {
  wchar_t buf[MAX_PATH] = { 0 };
  GetTempPathW(MAX_PATH, buf);
  std::srand((unsigned)std::time(nullptr));
  int rnd = std::rand() % 100000;
  temp_dir_ = std::wstring(buf) + L"packtmp" + std::to_wstring(rnd);
  CreateDirectoryW(temp_dir_.c_str(), nullptr);
  return true;
}

void PackInstall::SetCurrentFakeOutDir(const std::wstring& path) {
  int idx = DistInfo_AddFakeDir(&distinfo_, path.c_str());
  if (idx < 0) {
    XNSIS_LOG(L"DistInfo_AddFakeDir failed: %s", path.c_str());
    current_fake_idx_ = -1;
  }
  else {
    current_fake_idx_ = idx;
  }
}

uint64_t PackInstall::AddSrcFile(const std::wstring& path, int recurse, const std::set<std::wstring>& excluded) {
  if (completed_ || current_fake_idx_ < 0) {
    XNSIS_LOG(L"AddSrcFile called after completed or with invalid fake dir index");
    return 0;
  }
  std::set<std::wstring> before;
  ForEachFileRecursive(temp_dir_, temp_dir_, [&](const std::wstring& abs, const std::wstring& rel) {
    before.insert(rel);
    });
  std::wstring tar_path = temp_dir_ + L"\\addsrc_tmp.tar";
  std::wstring cmd = L"a -ttar";
  if (recurse) cmd += L" -r";
  for (const auto& ex : excluded) {
    cmd += L" -x!" + ex;
  }
  cmd += L" \"" + tar_path + L"\" \"" + path + L"\"";
  if (!SyncCall7zSync(cmd.c_str())) {
    XNSIS_LOG(L"SyncCall7zSync tar failed: %s", cmd.c_str());
    return 0;
  }
  std::wstring extract_cmd = L"x -ttar \"" + tar_path + L"\" -o\"" + temp_dir_ + L"\" -aos";
  if (!SyncCall7zSync(extract_cmd.c_str())) {
    XNSIS_LOG(L"SyncCall7zSync extract tar failed: %s", extract_cmd.c_str());
    return 0;
  }
  DeleteFileW(tar_path.c_str());
  ULONGLONG total_size = 0;
  ForEachFileRecursive(temp_dir_, temp_dir_, [&](const std::wstring& abs, const std::wstring& rel) {
    if (before.find(rel) == before.end()) {
      if (DistInfo_AddFile(&distinfo_, current_fake_idx_, rel.c_str()) == 0) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(abs.c_str(), GetFileExInfoStandard, &fad)) {
          ULONGLONG sz = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
          total_size += sz ? sz : 1;
        }
        else {
          XNSIS_LOG(L"GetFileAttributesExW failed: %s, error=%lu", abs.c_str(), GetLastError());
        }
      }
      else {
        XNSIS_LOG(L"DistInfo_AddFile failed: %s", rel.c_str());
      }
    }
    });
  return static_cast<int>(total_size);
}

uint64_t PackInstall::AddSrcFile(const std::wstring& path, const std::wstring& oname) {
  if (completed_ || current_fake_idx_ < 0) {
    XNSIS_LOG(L"AddSrcFile (single) called after completed or with invalid fake dir index");
    return 0;
  }
  std::wstring arc_path = oname;
  std::wstring dst = temp_dir_ + L"\\" + arc_path;
  if (!CreateDirRecursive(dst.substr(0, dst.find_last_of(L"\\/")))) {
    XNSIS_LOG(L"Failed to create directory for dst: %s", dst.c_str());
    return 0;
  }
  if (!CopyFileW(path.c_str(), dst.c_str(), FALSE)) {
    XNSIS_LOG(L"CopyFileW failed: %s -> %s, error=%lu", path.c_str(), dst.c_str(), GetLastError());
    return 0;
  }
  if (DistInfo_AddFile(&distinfo_, current_fake_idx_, arc_path.c_str()) != 0) {
    XNSIS_LOG(L"DistInfo_AddFile failed: %s", arc_path.c_str());
    return 0;
  }
  WIN32_FILE_ATTRIBUTE_DATA fad;
  ULONGLONG sz = 0;
  if (GetFileAttributesExW(dst.c_str(), GetFileExInfoStandard, &fad)) {
    sz = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
  }
  else {
    XNSIS_LOG(L"GetFileAttributesExW failed: %s, error=%lu", dst.c_str(), GetLastError());
  }
  return sz ? sz : 1;
}

bool PackInstall::ParseConfigIni() {
  compress_param_ = L"-t7z -m0=lzma:fb=273 -mx=9 -md=256M -ms=4G -mmt=2";
  pre_extract_plugins_.clear();

  HANDLE hFile = CreateFileW(L".\\config.ini", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    XNSIS_LOG(L"config.ini not found, using default compress_param");
    return true;
  }

  LARGE_INTEGER fileSize;
  if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart > 65536) {
    XNSIS_LOG(L"config.ini too large or invalid size");
    CloseHandle(hFile);
    return false;
  }
  DWORD size = (DWORD)fileSize.QuadPart;
  if (size == 0) {
    XNSIS_LOG(L"config.ini is empty, using default compress_param");
    CloseHandle(hFile);
    return true;
  }

  char* utf8_buffer = (char*)malloc(size + 1);
  if (!utf8_buffer) {
    XNSIS_LOG(L"malloc utf8_buffer failed");
    CloseHandle(hFile);
    return false;
  }
  DWORD bytesRead = 0;
  if (!ReadFile(hFile, utf8_buffer, size, &bytesRead, NULL) || bytesRead != size) {
    XNSIS_LOG(L"ReadFile failed");
    free(utf8_buffer);
    CloseHandle(hFile);
    return false;
  }
  utf8_buffer[size] = '\0';
  CloseHandle(hFile);

  int wide_size = MultiByteToWideChar(CP_UTF8, 0, utf8_buffer, -1, NULL, 0);
  if (wide_size <= 0) {
    XNSIS_LOG(L"MultiByteToWideChar size calculation failed, error=%lu", GetLastError());
    free(utf8_buffer);
    return false;
  }
  wchar_t* buffer = (wchar_t*)malloc(wide_size * sizeof(wchar_t));
  if (!buffer) {
    XNSIS_LOG(L"malloc buffer failed");
    free(utf8_buffer);
    return false;
  }
  if (MultiByteToWideChar(CP_UTF8, 0, utf8_buffer, -1, buffer, wide_size) <= 0) {
    XNSIS_LOG(L"MultiByteToWideChar conversion failed, error=%lu", GetLastError());
    free(buffer);
    free(utf8_buffer);
    return false;
  }
  free(utf8_buffer);

  // INI解析
  wchar_t* context = NULL;
  wchar_t* line = wcstok_s(buffer, L"\r\n", &context);
  bool in_compress_param = false;
  bool in_pre_extract_plugins = false;

  while (line) {
    // 跳过前导空白
    while (*line == L' ' || *line == L'\t') ++line;
    // 跳过空行和注释
    if (*line == L'\0' || *line == L'#' || *line == L';') {
      line = wcstok_s(NULL, L"\r\n", &context);
      continue;
    }
    // 节名
    if (*line == L'[') {
      wchar_t* end_bracket = wcschr(line, L']');
      if (end_bracket) {
        *end_bracket = L'\0';
        wchar_t* section = line + 1;
        while (*section == L' ' || *section == L'\t') ++section;
        wchar_t* section_end = section + wcslen(section) - 1;
        while (section_end > section && (*section_end == L' ' || *section_end == L'\t')) *section_end-- = L'\0';
        if (wcscmp(section, L"compress_param") == 0) {
          in_compress_param = true;
          in_pre_extract_plugins = false;
        }
        else if (wcscmp(section, L"pre_extract_plugins") == 0) {
          in_compress_param = false;
          in_pre_extract_plugins = true;
        }
        else {
          in_compress_param = false;
          in_pre_extract_plugins = false;
        }
      }
    }
    else {
      if (in_compress_param) {
        // 只取第一个非空行
        wchar_t* value = line;
        while (*value == L' ' || *value == L'\t') ++value;
        wchar_t* value_end = value + wcslen(value) - 1;
        while (value_end > value && (*value_end == L' ' || *value_end == L'\t')) *value_end-- = L'\0';
        if (*value) {
          compress_param_ = value;
          in_compress_param = false; // 只取一行
        }
      }
      else if (in_pre_extract_plugins) {
        wchar_t* colon = wcschr(line, L':');
        if (colon) {
          *colon = L'\0';
          wchar_t* path = line;
          wchar_t* param = colon + 1;
          while (*path == L' ' || *path == L'\t') ++path;
          wchar_t* path_end = path + wcslen(path) - 1;
          while (path_end > path && (*path_end == L' ' || *path_end == L'\t')) *path_end-- = L'\0';
          while (*param == L' ' || *param == L'\t') ++param;
          wchar_t* param_end = param + wcslen(param) - 1;
          while (param_end > param && (*param_end == L' ' || *param_end == L'\t')) *param_end-- = L'\0';
          if (*path && *param) {
            // 复制path到std::wstring并替换/
            std::wstring wpath(path);
            for (auto& ch : wpath) {
              if (ch == L'/') ch = L'\\';
            }
            PreExtractPlugin p;
            p.path = wpath;
            p.compress_param = param;
            pre_extract_plugins_.push_back(p);
          }
        }
      }
    }
    line = wcstok_s(NULL, L"\r\n", &context);
  }
  free(buffer);
  XNSIS_LOG(L"ParseConfigIni completed: compress_param=%s, pre_extract_plugins_count=%zu", compress_param_.c_str(), pre_extract_plugins_.size());
  return true;
}

BOOL WaitForDeleteFile(const LPCTSTR lpFileName, DWORD dwMilliseconds)
{
  // 参数校验
  if (lpFileName == NULL || lpFileName[0] == L'\0') {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  const DWORD dwStartTick = GetTickCount();
  DWORD dwRetryInterval = 100; // 重试间隔(ms)
  BOOL bSuccess = FALSE;

  do {
    // 尝试删除文件
    if (DeleteFile(lpFileName)) {
      bSuccess = TRUE;
      break;
    }

    // 检查是否因为文件不存在而失败
    if (GetLastError() == ERROR_FILE_NOT_FOUND) {
      bSuccess = TRUE;
      break;
    }

    // 检查是否超时
    if (GetTickCount() - dwStartTick > dwMilliseconds) {
      break;
    }

    // 动态调整重试间隔（指数退避算法）
    if (dwRetryInterval < 1000) {
      dwRetryInterval *= 2;
    }

    Sleep(dwRetryInterval);

  } while (TRUE);

  // 最终确认文件是否已删除
  if (bSuccess) {
    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile(lpFileName, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
      FindClose(hFind);
      bSuccess = FALSE; // 文件仍然存在
      SetLastError(ERROR_ACCESS_DENIED);
    }
  }

  return bSuccess;
}

bool PackInstall::GenerateInstall7z(CEXEBuild* build, int& build_compress) {
  if (completed_) {
    XNSIS_LOG(L"GenerateInstall7z called after completed");
    return false;
  }
  completed_ = true;

  // 生成带随机数的install.7z文件名
  std::srand((unsigned)std::time(nullptr));
  int rnd = std::rand() % 100000;
  install7z_name_ = L"install_" + std::to_wstring(rnd) + L".7z";
#ifdef DBG_SOLUTION
  install7z_path_ = install7z_name_;
#else
  install7z_path_ = temp_dir_ + L"\\" + install7z_name_;
#endif

  // 先处理pre_extract_plugins_列表中的文件
  for (const auto& plugin : pre_extract_plugins_) {
    // 检查插件文件是否存在于临时目录中
    std::wstring plugin_path = temp_dir_ + L"\\" + plugin.path;
    if (!IsDirExists(plugin_path) && GetFileAttributesW(plugin_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
      XNSIS_LOG(L"Plugin file not found: %s", plugin_path.c_str());
      continue;
    }

    // 创建.nsisbin文件夹
    std::wstring nsisbin_dir = temp_dir_ + L"\\" + plugin.path + L".nsisbin";
    if (!CreateDirRecursive(nsisbin_dir)) {
      XNSIS_LOG(L"Failed to create nsisbin directory: %s", nsisbin_dir.c_str());
      continue;
    }

    // 使用7z解压插件文件到.nsisbin文件夹
    std::wstring extract_cmd = L"x ";
    extract_cmd += plugin.compress_param;
    extract_cmd += L" \"" + plugin_path + L"\" -o\"" + nsisbin_dir + L"\" -aos";

    if (!SyncCall7zSync(extract_cmd.c_str())) {
      XNSIS_LOG(L"Failed to extract plugin: %s", plugin_path.c_str());
      continue;
    }

    if (!WaitForDeleteFile((LPCTSTR)plugin_path.c_str(), 5000)) {
      XNSIS_LOG(L"Failed to delete plugin: %s", plugin_path.c_str());
      return false;
    }

    XNSIS_LOG(L"Successfully extracted plugin: %s to %s", plugin_path.c_str(), nsisbin_dir.c_str());
  }

  // 打包所有文件到install.7z
  std::wstring param = GetConfig7zParam();
  std::wstring cmd = L"a ";
  cmd += param;
  cmd += L" \"" + install7z_path_ + L"\" \"" + temp_dir_ + L"\\*\"";
  if (!SyncCall7zSync(cmd.c_str())) {
    XNSIS_LOG(L"SyncCall7zSync 7z failed: %s", cmd.c_str());
    return false;
  }
  // 写分发信息

#ifdef DBG_SOLUTION
  std::wstring distinfo_path = g_dist_info_name;
#else
  std::wstring distinfo_path = temp_dir_ + L"\\" + distinfo_path;
#endif

  // 将install.7z文件名设置到distinfo中
  if (DistInfo_SetInstall7zName(&distinfo_, install7z_name_.c_str()) < 0) {
    XNSIS_LOG(L"DistInfo_SetInstall7zName failed");
    return false;
  }

  // 将pre_extract_plugins_信息添加到distinfo中
  for (const auto& plugin : pre_extract_plugins_) {
    if (DistInfo_AddPlugin(&distinfo_, plugin.path.c_str(), plugin.compress_param.c_str()) < 0) {
      XNSIS_LOG(L"DistInfo_AddPlugin failed: %s", plugin.path.c_str());
      return false;
    }
  }

  if (!DistInfo_Save(&distinfo_, distinfo_path.c_str())) {
    XNSIS_LOG(L"DistInfo_Save failed");
    return false;
  }
#ifndef  DBG_SOLUTION
  auto exec_script = [build](const std::wstring& cmd) {
    StringList hist;
    GrowBuf linedata;
    build->ps_addtoline(cmd.c_str(), linedata, hist);
    linedata.add(_T(""), sizeof(_T("")));
    return build->doParse((TCHAR*)linedata.get());
    };
  static const int cmd_count = 5;
  std::wstring cmd_list[cmd_count];
  cmd_list[0] = _T("SetCompress off");
  cmd_list[1] = _T("SetOutPath \"$INSTDIR\"");
  cmd_list[2] = std::wstring(_T("File /n7z ")) + install7z_path_;
  cmd_list[3] = std::wstring(_T("File /n7z ")) + distinfo_path;
  //off\0auto\0force\0
  switch (build_compress) {
  case 0: {
  } break;
  case 1: {
    cmd_list[4] = _T("SetCompress auto");
  } break;
  case 2: {
    cmd_list[4] = _T("SetCompress force");
  } break;
  }

  for (int i = 0; i < cmd_count; ++i) {
    if (cmd_list[i].empty()) {
      continue;
    }
    if (exec_script(cmd_list[i].c_str()) != PS_OK) {
      return false;
    }
  }
#endif // ! DBG_SOLUTION
  return true;
}

wchar_t* PackInstall::GetInstall7zName() {
  return const_cast<wchar_t*>(install7z_path_.c_str());
}

PackInstall::~PackInstall() {
  DistInfo_Free(&distinfo_);
  DeleteDirRecursiveW(temp_dir_);
}