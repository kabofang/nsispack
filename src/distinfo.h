#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

  // 信息块类型定义
#define DISTINFO_BLOCK_TYPE_DIRS        0x01  // 目录信息块
#define DISTINFO_BLOCK_TYPE_PLUGINS     0x02  // 插件信息块
#define DISTINFO_BLOCK_TYPE_INSTALL7Z   0x03  // install.7z文件名块
// 可以继续添加新的块类型...

  extern const wchar_t* g_dist_info_name;

  typedef struct {
    wchar_t* fake_dir;
    DWORD file_count;
    wchar_t** file_list;
  } InstallFakeDir;

  typedef struct {
    wchar_t* path;
    wchar_t* compress_param;
  } InstallPlugin;

  typedef struct {
    InstallFakeDir* dirs;
    DWORD dir_count;
    DWORD current_index;

    // 新增：插件信息
    InstallPlugin* plugins;
    DWORD plugin_count;
    
    // 新增：install.7z文件名（包含随机数）
    wchar_t* install7z_name;
  } InstallDistInfo;

  // 反序列化distinfo文件
  int DistInfo_Load(InstallDistInfo* info, const wchar_t* path);
  // 序列化distinfo文件
  int DistInfo_Save(const InstallDistInfo* info, const wchar_t* path);
  // 释放distinfo相关内存
  void DistInfo_Free(InstallDistInfo* info);
  // 添加一个fake目录，返回其索引（或-1失败）
  int DistInfo_AddFakeDir(InstallDistInfo* info, const wchar_t* fake_dir);
  // 向指定fake目录添加一个文件
  int DistInfo_AddFile(InstallDistInfo* info, int fake_dir_idx, const wchar_t* arc_path);
  // 添加一个插件信息
  int DistInfo_AddPlugin(InstallDistInfo* info, const wchar_t* path, const wchar_t* compress_param);
  // 设置install.7z文件名
  int DistInfo_SetInstall7zName(InstallDistInfo* info, const wchar_t* install7z_name);

#ifdef __cplusplus
}
#endif 