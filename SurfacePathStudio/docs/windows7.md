# Windows 7 SP1 x64 兼容版

产品为上海卫星装备研究所曲面轨迹生成软件 **0.1.2**，保留“中国航天 · 上海卫星装备研究所总装中心”标识与常规版功能。兼容版使用 Qt 5、MSVC 2019 和独立部署目录 `dist-win7-x64`。Windows 7 必须为安装了 Service Pack 1 的 64 位系统。

## 安装

安装文件名为 `上海卫星装备研究所曲面轨迹生成软件-0.1.2-Win7SP1-Qt5-x64-Setup.exe`。中文安装向导会检查 Windows 7 SP1 和 x64 架构；默认安装到 `%LOCALAPPDATA%\Programs\SurfacePathStudio`，无需管理员权限。航天蓝位图、机构名称、开始菜单、可选桌面快捷方式和卸载入口随安装包保留。

兼容版与常规 Qt 5 / Qt 6 版使用同一应用 GUID 和默认安装目录。在同一目录安装会更新当前安装。发布脚本使用不同部署目录、输出名和中间文件目录，原有 Windows 10 / Qt 6 安装包不被覆盖。

本包在主程序目录携带 Windows SDK 提供的应用本地 UCRT：`ucrtbase.dll` 与该 SDK x64 目录的全部 `api-ms-win-*.dll`，包括 CRT 和 core 转发 DLL。它们随应用安装与卸载，不写入系统目录。**本包不要求先安装 KB2999226**；该更新是另一种在系统目录集中部署 UCRT 的方式。Windows 7 上的插件需要从主程序目录找到这组文件，请保留完整安装目录。

三维查看仍需要厂商提供的 OpenGL 图形驱动，建议支持 OpenGL 3.3 或更高版本。OCCT 通过原生 WGL 显示，Qt 自带的软件 OpenGL DLL 不会自动替代这条渲染路径。首次在目标电脑使用时，运行软件、载入随附的平面或圆柱示例，检查旋转缩放、轨迹生成与 CSV 导出。

本次构建在当前开发机完成 42 / 42 项 CTest，桌面全流程验证通过，生成 499 个目标点。71 个 PE 文件通过静态依赖、已知较新 API、完整 UCRT 和原始来源 SHA256 检查。桌面报告记录目标 `Windows7SP1`，实际主机内核版本 `10.0.26200`，`ranOnWindows7=false`。**本机没有 Windows 7 实机或虚拟机，未进行 Win7 运行验收。** 静态检查与新系统回归不能替代目标电脑验证。

## 构建与打包

先在构建电脑使用 Qt 5.15 的 MSVC 2019 x64 SDK、MSVC 2019 工具链和 Windows SDK UCRT 再分发目录构建。示例路径按当前开发环境填写：

```powershell
./scripts/Build.ps1 -Windows7 `
  -QtRoot 'D:/Software/QT/content/5.15.0/msvc2019_64' `
  -UcrtRoot 'C:/Program Files (x86)/Windows Kits/10/Redist/10.0.19041.0/ucrt/DLLs/x64' `
  -BuildName win7-x64 -Deploy -DeployDirectory './dist-win7-x64' -RunTests

./scripts/Build-Installer.ps1 -TargetWindows Windows7SP1 -QtMajor 5 `
  -DistDirectory './dist-win7-x64' -ValidateOnly

./scripts/Build-Installer.ps1 -TargetWindows Windows7SP1 -QtMajor 5 `
  -DistDirectory './dist-win7-x64' `
  -InnoCompiler 'D:/Software/innosetup/content/ISCC.exe'
```

打包要求 `build-manifest.json` 声明 `TargetWindows=Windows7SP1`、`MinimumWindowsVersion=6.1sp1`、`ApplicationLocalUcrt=true`。脚本检查 UCRT 的完整精确清单、SHA256、四段文件版本和 x64 架构，并与 `UcrtSourceDirectory` 指定的原始 SDK 目录逐项核对。该目录必须在打包电脑可访问；迁移部署文件后需要提供与原清单一致的官方 SDK 源文件。

Direct3D 编译器使用 Qt 5 自带的 downlevel `d3dcompiler_47.dll`，其原路径与 SHA256 记入清单。正式编译安装包前，脚本在子 PowerShell 进程运行 `scripts/Test-Win7Compatibility.ps1`，核对完整 DLL 依赖及已知较新 API；报告位于 `releases/_packaging/Windows7SP1-Qt5/win7-static-audit.json`。`-ValidateOnly` 检查打包文件和来源并输出清单，不运行该真实 PE 依赖审计或编译安装程序。

兼容包只能使用 **Inno Setup 6**。现有 6.7.3 可以生成 Windows 7 SP1 安装程序；无需降级编译器。脚本读取 `ISCC /?` 的主版本标识，拒绝用 Inno Setup 7 打包兼容版。安装脚本条件设置 `MinVersion=6.1sp1`，使用原生 `modern` 向导样式；常规目标默认 `Windows10`，仍使用 `MinVersion=10.0` 与原有向导样式。

## 验证与来源

```powershell
./installer/Test-InstallerValidation.ps1
./installer/Test-Win7InstallerValidation.ps1
```

第二个测试使用合成应用文件和真实 SDK UCRT，验证 Qt 6 被拒绝、Win7 清单必填项、完整运行库、文件哈希与版本、介绍文件映射、未知文件排除和编译器主版本限制。它不安装软件，也不证明合成应用可以执行。

兼容依据：

- [Inno Setup 6.7.3 版本文档源码](https://raw.githubusercontent.com/jrsoftware/issrc/is-6_7_3/ISHelp/isetup.xml)：列出 Windows 7 支持，`MinVersion` 默认 `6.1sp1`。
- [Inno Setup 6 修订历史](https://jrsoftware.org/files/is6-whatsnew.htm)：6.3 移除 Vista 支持后，最低系统为 Windows 7；6.6 移除已失效的 `WizardResizable` 指令。
- [Microsoft Universal CRT deployment](https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment)：支持应用本地部署，建议包含 SDK 架构目录的全部 DLL；Windows 8 以前的系统应将这组 DLL 放在主程序目录。
- [Microsoft KB2999226](https://support.microsoft.com/en-us/servicing/os/windows/2020/04/update-for-universal-c-runtime-in-windows)：系统级 UCRT 更新适用于 Windows 7 SP1。
