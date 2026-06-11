#define MyAppName "GameSaveCloudQt"
#define MyAppVersion "0.3.0"
#define MyAppPublisher "baolibaobao"
#define MyAppExeName "GameSaveCloudQt.exe"
#define SourceDir "F:\ruanjian\yuncundang\release\GameSaveCloudQt-0.3.0"
#define OutputDir "F:\ruanjian\yuncundang\release\installer"

[Setup]
AppId={{56E6BD64-88F8-496E-A86C-A9A0E4E1B1E1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=GameSaveCloudQt-{#MyAppVersion}-Setup
SetupIconFile=F:\ruanjian\yuncundang\assets\icons\GameSaveCloud.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} installer
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加选项："; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent
