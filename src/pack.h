#pragma once
#include <string>
#include <set>
#include <vector>
#include "distinfo.h"

class CEXEBuild;
// pre_extract_plugins配置项结构
struct PreExtractPlugin {
    std::wstring path;      // 路径名
    std::wstring compress_param;  // 压缩参数
};

class PackInstall {
public:
  explicit PackInstall();
  ~PackInstall();

  void SetCurrentFakeOutDir(const std::wstring& path);
  uint64_t AddSrcFile(const std::wstring& path, int recurse, const std::set<std::wstring>& excluded);
  uint64_t AddSrcFile(const std::wstring& path, const std::wstring& oname);
  bool GenerateInstall7z(CEXEBuild* build, int& build_compress);
  const std::wstring& GetInstall7zPath();
  const std::wstring& GetDistInfoPath();
  
  // 获取pre_extract_plugins列表
  const std::vector<PreExtractPlugin>& GetPreExtractPlugins() const { return pre_extract_plugins_; }
  
  // 获取压缩参数
  std::wstring GetConfig7zParam() const { return compress_param_; }

private:
  InstallDistInfo distinfo_;
  int current_fake_idx_ = -1;
  std::wstring temp_dir_;
  std::wstring install7z_path_;
  std::wstring install7z_name_;
  std::wstring distinfo_path_;
  bool completed_ = false;
  bool need_pack_ = false;
  
  // config.ini相关
  std::wstring compress_param_;  // install7z压缩参数
  std::vector<PreExtractPlugin> pre_extract_plugins_;  // pre_extract_plugins列表

  bool InitTempDir();
  bool ParseConfigIni();  // 解析config.ini文件
  std::wstring GetCurrentModuleDir();
  bool SyncCall7zSync(const std::wstring& szCommand);
};