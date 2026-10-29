#ifndef AppVersion
  #define AppVersion "0.1.2-alpha"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\stage-installer"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#ifndef WebViewBootstrapper
  #define WebViewBootstrapper "..\dist\MicrosoftEdgeWebView2Setup.exe"
#endif

#define AppName "Local AI Runtime"
#define AppExe "LocalAIRuntime.Desktop.exe"
#define AppId "{{F4DFB343-79B4-4E84-8C5E-C3F064EADDD2}"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=azmisahin-ai
AppPublisherURL=https://github.com/azmisahin-ai/local-ai-runtime
AppSupportURL=https://github.com/azmisahin-ai/local-ai-runtime/issues
AppUpdatesURL=https://github.com/azmisahin-ai/local-ai-runtime/releases
DefaultDirName={localappdata}\Programs\Local AI Runtime
DefaultGroupName=Local AI Runtime
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
WizardStyle=modern
SetupIconFile=local-ai-runtime.ico
UninstallDisplayIcon={app}\LocalAIRuntime.Desktop.exe
OutputDir={#OutputDir}
OutputBaseFilename=local-ai-runtime-windows-x64-setup
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
AppMutex=Local\LocalAIRuntimeDesktopApp
Uninstallable=yes
UsePreviousAppDir=yes
UsePreviousGroup=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb"
Source: "{#WebViewBootstrapper}"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\Local AI Runtime"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\Local AI Runtime"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\MicrosoftEdgeWebView2Setup.exe"; Parameters: "/silent /install"; StatusMsg: "Preparing the Microsoft WebView runtime…"; Flags: runhidden waituntilterminated
Filename: "{app}\{#AppExe}"; Description: "Launch Local AI Runtime"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; User settings, engine packages, downloaded models and logs are stored in
; %LOCALAPPDATA%\LocalAIRuntime, outside {app}; deliberately preserve them.
