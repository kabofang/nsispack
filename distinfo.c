#include "distinfo.h"
#include "log.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wincrypt.h>
const wchar_t* g_dist_info_name = L"install.distinfo";

// MD5计算函数
static int CalculateMD5(const BYTE* data, DWORD data_len, BYTE* md5_out) {
  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;
  DWORD hash_len = 16; // MD5 is 16 bytes

  if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
    XNSIS_LOG(L"CryptAcquireContext failed, error=%lu", GetLastError());
    return 0;
  }

  if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
    XNSIS_LOG(L"CryptCreateHash failed, error=%lu", GetLastError());
    CryptReleaseContext(hProv, 0);
    return 0;
  }

  if (!CryptHashData(hHash, data, data_len, 0)) {
    XNSIS_LOG(L"CryptHashData failed, error=%lu", GetLastError());
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return 0;
  }

  if (!CryptGetHashParam(hHash, HP_HASHVAL, md5_out, &hash_len, 0)) {
    XNSIS_LOG(L"CryptGetHashParam failed, error=%lu", GetLastError());
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return 0;
  }

  CryptDestroyHash(hHash);
  CryptReleaseContext(hProv, 0);
  return 1;
}

// 写入块头部
static int WriteBlockHeader(BYTE** p, BYTE type, DWORD length) {
  **p = type; (*p)++;
  *(DWORD*)*p = length; (*p) += 4;
  return 1;
}

// 读取块头部
static int ReadBlockHeader(BYTE** p, BYTE* type, DWORD* length, BYTE* end) {
  if (*p + 5 > end) return 0;
  *type = **p; (*p)++;
  *length = *(DWORD*)*p; (*p) += 4;
  return 1;
}

int DistInfo_Load(InstallDistInfo* info, const wchar_t* filename) {
  // 初始化结构
  memset(info, 0, sizeof(InstallDistInfo));

  HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    XNSIS_LOG(L"CreateFileW failed: %s, error=%lu", filename, GetLastError());
    return 0;
  }

  LARGE_INTEGER fsize;
  if (!GetFileSizeEx(hFile, &fsize) || fsize.QuadPart < 24) { // 至少需要8字节头部+16字节MD5
    XNSIS_LOG(L"GetFileSizeEx failed or file too small: %s", filename);
    CloseHandle(hFile); return 0;
  }

  DWORD size = (DWORD)fsize.QuadPart;
  BYTE* buffer = (BYTE*)malloc(size);
  if (!buffer) {
    XNSIS_LOG(L"malloc buffer failed");
    CloseHandle(hFile); return 0;
  }

  DWORD read = 0;
  if (!ReadFile(hFile, buffer, size, &read, NULL) || read != size) {
    XNSIS_LOG(L"ReadFile failed, error=%lu", GetLastError());
    free(buffer); CloseHandle(hFile); return 0;
  }
  CloseHandle(hFile);

  // 验证MD5
  DWORD data_len = size - 16; // 减去MD5长度
  BYTE expected_md5[16];
  memcpy(expected_md5, buffer + data_len, 16);

  BYTE calculated_md5[16];
  if (!CalculateMD5(buffer, data_len, calculated_md5)) {
    XNSIS_LOG(L"CalculateMD5 failed during verification");
    free(buffer); return 0;
  }

  if (memcmp(expected_md5, calculated_md5, 16) != 0) {
    XNSIS_LOG(L"MD5 verification failed - file may be corrupted");
    free(buffer); return 0;
  }

  BYTE* p = buffer;
  BYTE* end = buffer + data_len; // 不包括MD5部分

  // 检查magic和version
  if (p + 8 > end || memcmp(p, "XNSI", 4) != 0) {
    XNSIS_LOG(L"Magic mismatch");
    free(buffer); return 0;
  }
  p += 4;
  DWORD version = *(DWORD*)p; p += 4;
  if (version != 2) {
    XNSIS_LOG(L"Unsupported version: %lu", version);
    free(buffer); return 0;
  }

  // 解析各个信息块
  while (p < end) {
    BYTE block_type;
    DWORD block_length;
    if (!ReadBlockHeader(&p, &block_type, &block_length, end)) {
      XNSIS_LOG(L"Failed to read block header");
      free(buffer); return 0;
    }

    if (p + block_length > end) {
      XNSIS_LOG(L"Block length exceeds file size");
      free(buffer); return 0;
    }

    BYTE* block_end = p + block_length;

    switch (block_type) {
    case DISTINFO_BLOCK_TYPE_DIRS: {
      // 解析目录信息
      if (p + 4 > block_end) { free(buffer); return 0; }
      DWORD dir_count = *(DWORD*)p; p += 4;

      info->dirs = (InstallFakeDir*)calloc(dir_count, sizeof(InstallFakeDir));
      if (!info->dirs) { free(buffer); return 0; }
      info->dir_count = dir_count;

      for (DWORD i = 0; i < dir_count; ++i) {
        if (p + 4 > block_end) { free(buffer); return 0; }
        DWORD len = *(DWORD*)p; p += 4;
        if (p + len * sizeof(wchar_t) > block_end) { free(buffer); return 0; }

        info->dirs[i].fake_dir = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
        if (!info->dirs[i].fake_dir) { free(buffer); return 0; }
        memcpy(info->dirs[i].fake_dir, p, len * sizeof(wchar_t));
        info->dirs[i].fake_dir[len] = L'\0';
        p += len * sizeof(wchar_t);

        if (p + 4 > block_end) { free(buffer); return 0; }
        DWORD file_count = *(DWORD*)p; p += 4;
        info->dirs[i].file_count = file_count;

        info->dirs[i].file_list = (wchar_t**)calloc(file_count, sizeof(wchar_t*));
        if (!info->dirs[i].file_list) { free(buffer); return 0; }

        for (DWORD j = 0; j < file_count; ++j) {
          if (p + 4 > block_end) { free(buffer); return 0; }
          DWORD plen = *(DWORD*)p; p += 4;
          if (p + plen * sizeof(wchar_t) > block_end) { free(buffer); return 0; }

          info->dirs[i].file_list[j] = (wchar_t*)malloc((plen + 1) * sizeof(wchar_t));
          if (!info->dirs[i].file_list[j]) { free(buffer); return 0; }
          memcpy(info->dirs[i].file_list[j], p, plen * sizeof(wchar_t));
          info->dirs[i].file_list[j][plen] = L'\0';
          p += plen * sizeof(wchar_t);
        }
      }
      break;
    }

    case DISTINFO_BLOCK_TYPE_PLUGINS: {
      // 解析插件信息
      if (p + 4 > block_end) { free(buffer); return 0; }
      DWORD plugin_count = *(DWORD*)p; p += 4;
      
      info->plugins = (InstallPlugin*)calloc(plugin_count, sizeof(InstallPlugin));
      if (!info->plugins) { free(buffer); return 0; }
      info->plugin_count = plugin_count;
      
      for (DWORD i = 0; i < plugin_count; ++i) {
        // 读取path
        if (p + 4 > block_end) { free(buffer); return 0; }
        DWORD path_len = *(DWORD*)p; p += 4;
        if (p + path_len * sizeof(wchar_t) > block_end) { free(buffer); return 0; }
        
        info->plugins[i].path = (wchar_t*)malloc((path_len + 1) * sizeof(wchar_t));
        if (!info->plugins[i].path) { free(buffer); return 0; }
        memcpy(info->plugins[i].path, p, path_len * sizeof(wchar_t));
        info->plugins[i].path[path_len] = L'\0';
        p += path_len * sizeof(wchar_t);
        
        // 读取compress_param
        if (p + 4 > block_end) { free(buffer); return 0; }
        DWORD param_len = *(DWORD*)p; p += 4;
        if (p + param_len * sizeof(wchar_t) > block_end) { free(buffer); return 0; }
        
        info->plugins[i].compress_param = (wchar_t*)malloc((param_len + 1) * sizeof(wchar_t));
        if (!info->plugins[i].compress_param) { free(buffer); return 0; }
        memcpy(info->plugins[i].compress_param, p, param_len * sizeof(wchar_t));
        info->plugins[i].compress_param[param_len] = L'\0';
        p += param_len * sizeof(wchar_t);
      }
      break;
    }

    case DISTINFO_BLOCK_TYPE_INSTALL7Z: {
      // 解析install.7z文件名
      if (p + 4 > block_end) { free(buffer); return 0; }
      DWORD name_len = *(DWORD*)p; p += 4;
      if (p + name_len * sizeof(wchar_t) > block_end) { free(buffer); return 0; }
      
      info->install7z_name = (wchar_t*)malloc((name_len + 1) * sizeof(wchar_t));
      if (!info->install7z_name) { free(buffer); return 0; }
      memcpy(info->install7z_name, p, name_len * sizeof(wchar_t));
      info->install7z_name[name_len] = L'\0';
      p += name_len * sizeof(wchar_t);
      break;
    }

    default:
      // 跳过未知的块类型
      XNSIS_LOG(L"Unknown block type: %d, skipping", block_type);
      p = block_end;
      break;
    }
  }

  free(buffer);
  return 1;
}

int DistInfo_Save(const InstallDistInfo* info, const wchar_t* filename) {
  // 计算总大小（不包括MD5）
  size_t total = 8; // magic + version

  // 目录信息块大小
  size_t dirs_block_size = 4; // dir_count
  for (DWORD i = 0; i < info->dir_count; ++i) {
    const InstallFakeDir* dir = &info->dirs[i];
    DWORD len = (DWORD)wcslen(dir->fake_dir);
    dirs_block_size += 4 + len * sizeof(wchar_t); // fake_dir len + name
    dirs_block_size += 4; // file_count
    for (DWORD j = 0; j < dir->file_count; ++j) {
      DWORD plen = (DWORD)wcslen(dir->file_list[j]);
      dirs_block_size += 4 + plen * sizeof(wchar_t); // arc_path len + name
    }
  }
  total += 5 + dirs_block_size; // block header + content

  // 插件信息块大小
  size_t plugins_block_size = 4; // plugin_count
  for (DWORD i = 0; i < info->plugin_count; ++i) {
    const InstallPlugin* plugin = &info->plugins[i];
    DWORD path_len = (DWORD)wcslen(plugin->path);
    DWORD param_len = (DWORD)wcslen(plugin->compress_param);
    plugins_block_size += 4 + path_len * sizeof(wchar_t); // path len + path
    plugins_block_size += 4 + param_len * sizeof(wchar_t); // param len + param
  }
  total += 5 + plugins_block_size; // block header + content

  // install.7z文件名块大小
  size_t install7z_block_size = 0;
  if (info->install7z_name) {
    DWORD name_len = (DWORD)wcslen(info->install7z_name);
    install7z_block_size = 4 + name_len * sizeof(wchar_t); // name len + name
    total += 5 + install7z_block_size; // block header + content
  }

  // 添加MD5大小
  total += 16; // MD5 hash

  BYTE* buffer = (BYTE*)malloc(total);
  if (!buffer) {
    XNSIS_LOG(L"malloc buffer failed");
    return 0;
  }

  BYTE* p = buffer;

  // 写入文件头
  memcpy(p, "XNSI", 4); p += 4;
  *(DWORD*)p = 2; p += 4; // version 2

  // 写入目录信息块
  WriteBlockHeader(&p, DISTINFO_BLOCK_TYPE_DIRS, (DWORD)dirs_block_size);
  *(DWORD*)p = info->dir_count; p += 4;

  for (DWORD i = 0; i < info->dir_count; ++i) {
    const InstallFakeDir* dir = &info->dirs[i];
    DWORD len = (DWORD)wcslen(dir->fake_dir);
    *(DWORD*)p = len; p += 4;

    // 复制路径并替换/为\
        {
    wchar_t* fake_dir_buf = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!fake_dir_buf) { XNSIS_LOG(L"malloc fake_dir_buf failed"); free(buffer); return 0; }
    wcsncpy_s(fake_dir_buf, len + 1, dir->fake_dir, len);
    for (DWORD k = 0; k < len; ++k) if (fake_dir_buf[k] == L'/') fake_dir_buf[k] = L'\\';
    memcpy(p, fake_dir_buf, len * sizeof(wchar_t)); p += len * sizeof(wchar_t);
    free(fake_dir_buf);
  }

  *(DWORD*)p = dir->file_count; p += 4;

  for (DWORD j = 0; j < dir->file_count; ++j) {
    DWORD plen = (DWORD)wcslen(dir->file_list[j]);
    *(DWORD*)p = plen; p += 4;

    // 复制文件路径并替换/为\
            {
    wchar_t* file_path_buf = (wchar_t*)malloc((plen + 1) * sizeof(wchar_t));
    if (!file_path_buf) { XNSIS_LOG(L"malloc file_path_buf failed"); free(buffer); return 0; }
    wcsncpy_s(file_path_buf, plen + 1, dir->file_list[j], plen);
    for (DWORD k = 0; k < plen; ++k) if (file_path_buf[k] == L'/') file_path_buf[k] = L'\\';
    memcpy(p, file_path_buf, plen * sizeof(wchar_t)); p += plen * sizeof(wchar_t);
    free(file_path_buf);
  }
}
    }

    // 写入插件信息块
    WriteBlockHeader(&p, DISTINFO_BLOCK_TYPE_PLUGINS, (DWORD)plugins_block_size);
    *(DWORD*)p = info->plugin_count; p += 4;

    for (DWORD i = 0; i < info->plugin_count; ++i) {
      const InstallPlugin* plugin = &info->plugins[i];
      DWORD path_len = (DWORD)wcslen(plugin->path);
      DWORD param_len = (DWORD)wcslen(plugin->compress_param);

      *(DWORD*)p = path_len; p += 4;

      // 复制插件路径并替换/为\
        {
      wchar_t* plugin_path_buf = (wchar_t*)malloc((path_len + 1) * sizeof(wchar_t));
      if (!plugin_path_buf) { XNSIS_LOG(L"malloc plugin_path_buf failed"); free(buffer); return 0; }
      wcsncpy_s(plugin_path_buf, path_len + 1, plugin->path, path_len);
      for (DWORD k = 0; k < path_len; ++k) if (plugin_path_buf[k] == L'/') plugin_path_buf[k] = L'\\';
      memcpy(p, plugin_path_buf, path_len * sizeof(wchar_t)); p += path_len * sizeof(wchar_t);
      free(plugin_path_buf);
    }

    *(DWORD*)p = param_len; p += 4;
    memcpy(p, plugin->compress_param, param_len * sizeof(wchar_t)); p += param_len * sizeof(wchar_t);
    }

    // 写入install.7z文件名块
    if (info->install7z_name) {
        WriteBlockHeader(&p, DISTINFO_BLOCK_TYPE_INSTALL7Z, (DWORD)install7z_block_size);
        DWORD name_len = (DWORD)wcslen(info->install7z_name);
        *(DWORD*)p = name_len; p += 4;
        
        // 复制文件名并替换/为\
        {
            wchar_t* name_buf = (wchar_t*)malloc((name_len + 1) * sizeof(wchar_t));
            if (!name_buf) { XNSIS_LOG(L"malloc name_buf failed"); free(buffer); return 0; }
            wcsncpy_s(name_buf, name_len + 1, info->install7z_name, name_len);
            for (DWORD k = 0; k < name_len; ++k) if (name_buf[k] == L'/') name_buf[k] = L'\\';
            memcpy(p, name_buf, name_len * sizeof(wchar_t)); p += name_len * sizeof(wchar_t);
            free(name_buf);
        }
    }
    
    // 计算并写入MD5（不包括MD5本身）
    DWORD data_len = (DWORD)(p - buffer);
    BYTE md5_hash[16];
    if (!CalculateMD5(buffer, data_len, md5_hash)) {
        XNSIS_LOG(L"CalculateMD5 failed");
        free(buffer); return 0;
    }
    memcpy(p, md5_hash, 16);

    // 写入文件
    HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
      XNSIS_LOG(L"CreateFileW failed: %s, error=%lu", filename, GetLastError());
      free(buffer); return 0;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, buffer, (DWORD)total, &written, NULL);
    if (!ok || written != (DWORD)total) {
      XNSIS_LOG(L"WriteFile failed, error=%lu", GetLastError());
      CloseHandle(hFile); free(buffer); return 0;
    }

    CloseHandle(hFile);
    free(buffer);
    return 1;
}

void DistInfo_Free(InstallDistInfo* info) {
  if (!info) return;

  // 释放目录信息
  if (info->dirs) {
    for (DWORD i = 0; i < info->dir_count; ++i) {
      for (DWORD j = 0; j < info->dirs[i].file_count; ++j) {
        free(info->dirs[i].file_list[j]);
      }
      free(info->dirs[i].file_list);
      free(info->dirs[i].fake_dir);
    }
    free(info->dirs);
  }

  // 释放插件信息
  if (info->plugins) {
    for (DWORD i = 0; i < info->plugin_count; ++i) {
      free(info->plugins[i].path);
      free(info->plugins[i].compress_param);
    }
    free(info->plugins);
  }

  // 释放install.7z文件名
  if (info->install7z_name) {
    free(info->install7z_name);
  }
  
  memset(info, 0, sizeof(InstallDistInfo));
}

int DistInfo_AddFakeDir(InstallDistInfo* info, const wchar_t* fake_dir) {
  if (!info || !fake_dir) return -1;
  DWORD new_count = info->dir_count + 1;
  InstallFakeDir* new_dirs = (InstallFakeDir*)realloc(info->dirs, new_count * sizeof(InstallFakeDir));
  if (!new_dirs) return -1;
  info->dirs = new_dirs;
  InstallFakeDir* fdir = &info->dirs[new_count - 1];
  size_t len = wcslen(fake_dir);
  fdir->fake_dir = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
  if (!fdir->fake_dir) return -1;
  wcsncpy_s(fdir->fake_dir, len + 1, fake_dir, len);
  fdir->fake_dir[len] = 0;
  fdir->file_count = 0;
  fdir->file_list = NULL;
  info->dir_count = new_count;
  return (int)(new_count - 1);
}

int DistInfo_AddFile(InstallDistInfo* info, int fake_dir_idx, const wchar_t* arc_path) {
  if (!info || fake_dir_idx < 0 || (DWORD)fake_dir_idx >= info->dir_count || !arc_path) return -1;
  InstallFakeDir* fdir = &info->dirs[fake_dir_idx];
  DWORD new_count = fdir->file_count + 1;
  wchar_t** new_list = (wchar_t**)realloc(fdir->file_list, new_count * sizeof(wchar_t*));
  if (!new_list) return -1;
  fdir->file_list = new_list;
  size_t len = wcslen(arc_path);
  fdir->file_list[fdir->file_count] = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
  if (!fdir->file_list[fdir->file_count]) return -1;
  wcsncpy_s(fdir->file_list[fdir->file_count], len + 1, arc_path, len);
  fdir->file_list[fdir->file_count][len] = 0;
  fdir->file_count = new_count;
  return 0;
}

int DistInfo_AddPlugin(InstallDistInfo* info, const wchar_t* path, const wchar_t* compress_param) {
  if (!info || !path || !compress_param) return -1;
  DWORD new_count = info->plugin_count + 1;
  InstallPlugin* new_plugins = (InstallPlugin*)realloc(info->plugins, new_count * sizeof(InstallPlugin));
  if (!new_plugins) return -1;
  info->plugins = new_plugins;
  InstallPlugin* plugin = &info->plugins[new_count - 1];
  
  size_t path_len = wcslen(path);
  plugin->path = (wchar_t*)malloc((path_len + 1) * sizeof(wchar_t));
  if (!plugin->path) return -1;
  wcsncpy_s(plugin->path, path_len + 1, path, path_len);
  plugin->path[path_len] = 0;
  
  size_t param_len = wcslen(compress_param);
  plugin->compress_param = (wchar_t*)malloc((param_len + 1) * sizeof(wchar_t));
  if (!plugin->compress_param) {
    free(plugin->path);
    return -1;
  }
  wcsncpy_s(plugin->compress_param, param_len + 1, compress_param, param_len);
  plugin->compress_param[param_len] = 0;
  
  info->plugin_count = new_count;
  return (int)(new_count - 1);
}

int DistInfo_SetInstall7zName(InstallDistInfo* info, const wchar_t* install7z_name) {
    if (!info || !install7z_name) return -1;
    
    // 释放旧的名称
    if (info->install7z_name) {
        free(info->install7z_name);
        info->install7z_name = NULL;
    }
    
    // 分配并复制新名称
    size_t name_len = wcslen(install7z_name);
    info->install7z_name = (wchar_t*)malloc((name_len + 1) * sizeof(wchar_t));
    if (!info->install7z_name) return -1;
    
    wcsncpy_s(info->install7z_name, name_len + 1, install7z_name, name_len);
    info->install7z_name[name_len] = 0;
    
    return 0;
}