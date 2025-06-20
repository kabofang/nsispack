#include "pack.h"
#include "install.h"

#define CHECK_ADDSRC_ERROR(exp) if(!(exp)) DebugBreak();

int main() {
  PackInstall instance{};
  instance.SetCurrentFakeOutDir(L"$1");
 CHECK_ADDSRC_ERROR(instance.AddSrcFile(L".\\test\\in\\fake1_rec1", true, std::set<std::wstring>{}));
  CHECK_ADDSRC_ERROR(instance.AddSrcFile(L".\\test\\in\\fake1_1.txt", true, std::set<std::wstring>{}));
  CHECK_ADDSRC_ERROR(instance.AddSrcFile(L".\\test\\in\\fake1_2.txt", L"fake12txt/fake1_2o.txt"));
  instance.SetCurrentFakeOutDir(L"$2");
  CHECK_ADDSRC_ERROR(instance.AddSrcFile(L".\\test\\in\\ff3\\content.pkg", L"ff3\\content.pkg"));
  CHECK_ADDSRC_ERROR(instance.AddSrcFile(L".\\test\\in\\fake2_1.txt", L"fake21txt\\fake2_1o.txt"));
  CHECK_ADDSRC_ERROR(instance.AddSrcFile(L".\\test\\in\\fake2_2.txt", true, std::set<std::wstring>{}));
  CHECK_ADDSRC_ERROR(instance.AddSrcFile(L".\\test\\in\\fake2_3.txt", L"fake21txt\\fake2_3o.txt"));
  CHECK_ADDSRC_ERROR(instance.AddSrcFile(L".\\test\\in\\fake2_4.txt", L"fake21txt\\fake2_4o.txt"));
  int x;
  instance.GenerateInstall7z(nullptr, x);

  InstallContext context = InstallContext{};

  CHECK_ADDSRC_ERROR(SetCurrentRealOutDir(&context, L"test\\out\\$11"));
  CHECK_ADDSRC_ERROR(SetCurrentRealOutDir(&context, L"test\\out\\$12"));
  CHECK_ADDSRC_ERROR(InstallContext_Init(&context, g_dist_info_name));
  CHECK_ADDSRC_ERROR(ExtractInstall7z(&context));
  InstallContext_Free(&context);
}