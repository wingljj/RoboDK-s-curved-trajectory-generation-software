# Windows 安装与发布

产品：**上海卫星装备研究所曲面轨迹生成软件 0.1.2**。安装页、软件介绍和快捷方式显示“中国航天”和“上海卫星装备研究所总装中心”，安装与卸载使用中国航天标识。

默认发布 Qt 5 x64 安装包，另可构建 Qt 6 x64。两种构建使用同一个应用标识，属于同一软件的两种运行库版本；在同一个目录安装会更新当前安装，不用于同时安装两份独立产品。

## 安装与卸载

双击 `上海卫星装备研究所曲面轨迹生成软件-0.1.2-Qt5-x64-Setup.exe`，按中文向导阅读软件介绍、选择目录与快捷方式。默认安装到 `%LOCALAPPDATA%\Programs\SurfacePathStudio`，仅为当前用户安装，无需管理员权限。系统要求为 Windows 10 或更高版本的 x64 兼容系统。

开始菜单包含软件启动、软件介绍与卸载入口，桌面快捷方式可选。也可从 Windows“设置 → 应用”卸载。卸载只删除安装程序登记的文件；软件运行时另行导出的 CSV 不被递归清理。

安装目录包含 `软件介绍.txt`、`README.md`、`docs`、`examples\models`、`THIRD_PARTY_NOTICES.md` 与 `licenses`。STEP/STP 通过软件的打开或拖入操作载入，安装程序不注册文件关联。

## 构建安装包

先使用匹配的 Qt SDK 构建到新的 Release 部署目录。请保留正在使用的原 `dist` / `dist-qt5`，不要将用户导出内容混入发布目录。

```powershell
./scripts/Build.ps1 -QtRoot 'D:/Software/QT/content/5.15.0/msvc2019_64' `
  -BuildName branding-qt5 -RunTests -Deploy -DeployDirectory './dist-branding-qt5'
./scripts/Build-Installer.ps1
```

Qt 6 安装包：

```powershell
./scripts/Build.ps1 -QtRoot 'D:/Software/QT/content/6.5.3/msvc2019_64' `
  -BuildName branding-qt6 -RunTests -Deploy -DeployDirectory './dist-branding-qt6'
./scripts/Build-Installer.ps1 -QtMajor 6
```

指定现有 Inno Setup 编译器和输出位置：

```powershell
./scripts/Build-Installer.ps1 -QtMajor 5 `
  -DistDirectory './dist-branding-qt5' -OutputDirectory './releases' `
  -InnoCompiler 'D:/Software/innosetup/content/ISCC.exe'
```

`-ValidateOnly` 只检查部署内容并输出打包清单，不编译或安装。脚本确认构建清单的版本 `0.1.2`、Qt 主版本、x64、Release 与 EXE SHA256，并检查每个入包 EXE/DLL 的 PE 架构和必要运行库。安装资源使用 `resources/casc.ico` 与基本/高清向导位图，中文信息文件保存为 UTF-8。

输出位于 `releases`：安装 EXE、对应 `.sha256` 和 `-payload.json`。`_packaging/Qt5` 或 `_packaging/Qt6` 内保留编译用精确文件清单。文件清单仅选取指定运行库、指定 Qt 插件、七个示例模型、许可、README、构建清单、`docs/*.md` 和验证图片/JSON；CSV 只允许文档中明确列出的两个验证样例。根目录的用户 CSV、任意 EXE、日志、测试文件和未知 DLL 不入包。

Qt 与 Open CASCADE 的许可文本随软件保留。应用及安装包没有配置代码签名，Windows 展示的发布者签名状态以实际文件为准。

## 隔离验证

在没有同一 AppId 的既有安装时，可以指定临时目录与独立开始菜单组进行静默验证。若已有本产品安装，请使用独立 Windows 用户或测试虚拟机，以免同一应用标识更新当前用户的卸载登记。

```powershell
$setup = Join-Path $PWD 'releases/上海卫星装备研究所曲面轨迹生成软件-0.1.2-Qt5-x64-Setup.exe'
$testInstallDirectory = Join-Path $env:TEMP 'SurfacePathStudio-InstallerCheck'
$installLog = Join-Path $env:TEMP 'SurfacePathStudio-InstallCheck.log'
$setupArguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-',
  "/DIR=`"$testInstallDirectory`"", '/GROUP="SurfacePathStudio-InstallerCheck"',
  '/MERGETASKS="!desktopicon"', "/LOG=`"$installLog`"")
$setupProcess = Start-Process -FilePath $setup -ArgumentList $setupArguments `
  -WindowStyle Hidden -Wait -PassThru
$setupProcess.ExitCode
& (Join-Path $testInstallDirectory 'SurfacePathStudio.exe')

$uninstallLog = Join-Path $env:TEMP 'SurfacePathStudio-UninstallCheck.log'
$uninstallProcess = Start-Process -FilePath (Join-Path $testInstallDirectory 'unins000.exe') `
  -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$uninstallLog`"") `
  -WindowStyle Hidden -Wait -PassThru
$uninstallProcess.ExitCode
```

验证应检查安装/卸载退出码、中文快捷方式、当前用户卸载登记的产品名与图标、应用启动、示例载入、许可和介绍文件，以及根目录用户 CSV 未入包。安装前后的 `.stp/.step` 文件关联应保持原样。静默参数与用户级安装行为见 [Inno Setup 命令行文档](https://jrsoftware.org/ishelp/topic_setupcmdline.htm) 和 [PrivilegesRequired 文档](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm)；多尺寸向导图像见 [WizardImageFile 文档](https://jrsoftware.org/ishelp/topic_setup_wizardimagefile.htm)。
