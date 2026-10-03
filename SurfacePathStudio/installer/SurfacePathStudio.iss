; Build with scripts/Build-Installer.ps1. The generated include contains only approved files.
#ifndef ProjectRoot
  #error "ProjectRoot is required; use scripts/Build-Installer.ps1."
#endif
#ifndef PayloadFileList
  #error "PayloadFileList is required; use scripts/Build-Installer.ps1."
#endif
#ifndef InstallerOutputDirectory
  #error "InstallerOutputDirectory is required."
#endif
#ifndef InstallerOutputBaseName
  #error "InstallerOutputBaseName is required."
#endif
#ifndef QtMajor
  #define QtMajor "5"
#endif

#define AppName "上海卫星装备研究所曲面轨迹生成软件"
#define AppVersion "0.1.2"
#define AppPublisher "上海卫星装备研究所总装中心"
#define AppExe "SurfacePathStudio.exe"
#define ResourceRoot ProjectRoot + "\resources"
#define WizardImages ResourceRoot + "\installer-wizard.bmp"
#define SmallImages ResourceRoot + "\installer-small.bmp"
#if FileExists(ResourceRoot + "\installer-wizard@2x.bmp")
  #define WizardImages WizardImages + "," + ResourceRoot + "\installer-wizard@2x.bmp"
#endif
#if FileExists(ResourceRoot + "\installer-small@3x.bmp")
  #define SmallImages SmallImages + "," + ResourceRoot + "\installer-small@3x.bmp"
#endif

[Setup]
AppId={{DF364F38-EF55-4A1C-84A2-8990C00E7AD4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppComments=中国航天 · 上海卫星装备研究所总装中心
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName={#AppName}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName}安装程序（Qt{#QtMajor} x64）
DefaultDirName={localappdata}\Programs\SurfacePathStudio
DefaultGroupName={#AppName}
DisableProgramGroupPage=no
DisableDirPage=no
DisableWelcomePage=no
DisableFinishedPage=no
AllowNoIcons=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#InstallerOutputDirectory}
OutputBaseFilename={#InstallerOutputBaseName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern light polar
WizardResizable=yes
WizardImageFile={#WizardImages}
WizardSmallImageFile={#SmallImages}
WizardImageStretch=yes
WizardImageBackColor=$3D2110
WizardSmallImageBackColor=white
SetupIconFile={#ResourceRoot}\casc.ico
InfoBeforeFile={#ProjectRoot}\installer\软件介绍.txt
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\resources\casc.ico
Uninstallable=yes
CreateUninstallRegKey=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Messages]
WelcomeLabel1=上海卫星装备研究所%n曲面轨迹生成软件安装向导
WelcomeLabel2=中国航天%n上海卫星装备研究所总装中心%n%n本向导将安装 {#AppName} {#AppVersion}（Qt{#QtMajor} x64）。
InfoBeforeLabel=软件介绍
InfoBeforeClickLabel=阅读软件介绍后，点击“下一步”继续。
FinishedLabel=已完成 {#AppName} 的安装。%n%n您可以通过开始菜单或桌面快捷方式启动软件，载入 STEP/STP 模型并生成曲面轨迹。
FinishedLabelNoIcons=已完成 {#AppName} 的安装。%n%n您可以从安装目录启动软件，载入 STEP/STP 模型并生成曲面轨迹。

[CustomMessages]
DesktopShortcut=创建桌面快捷方式
ShortcutGroup=快捷方式：
LaunchApplication=启动{#AppName}
OpenIntroduction=查看软件介绍
UninstallApplication=卸载{#AppName}

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; GroupDescription: "{cm:ShortcutGroup}"; Flags: unchecked

[Files]
#include PayloadFileList
Source: "{#ProjectRoot}\resources\casc.ico"; DestDir: "{app}\resources"; Flags: ignoreversion
Source: "{#ProjectRoot}\installer\软件介绍.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; IconFilename: "{app}\resources\casc.ico"; Comment: "中国航天 · {#AppPublisher}"
Name: "{group}\软件介绍"; Filename: "{app}\软件介绍.txt"; IconFilename: "{app}\resources\casc.ico"
Name: "{group}\{cm:UninstallApplication}"; Filename: "{uninstallexe}"; IconFilename: "{app}\resources\casc.ico"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; IconFilename: "{app}\resources\casc.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchApplication}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\软件介绍.txt"; Description: "{cm:OpenIntroduction}"; Flags: shellexec postinstall skipifsilent unchecked

; No file associations or recursive uninstall deletion: user exports are retained.
