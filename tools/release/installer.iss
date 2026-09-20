#ifndef StageDir
  #error StageDir required
#endif
#ifndef OutputDir
  #error OutputDir required
#endif
#define AppName "Parallel Finder"
#define AppVersion "0.1.0-rc.2"
[Setup]
AppId={{C8F36BC9-6469-42F4-97D1-0421C92638E5}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Pozit1vchic
AppPublisherURL=https://github.com/Pozit1vchic/Parallel-Finder
DefaultDirName={localappdata}\Programs\ParallelFinder
DefaultGroupName=Parallel Finder
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=ParallelFinder-{#AppVersion}-Setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\..\app\ParallelFinder.ico
LicenseFile={#StageDir}\LICENSE
UninstallDisplayIcon={app}\ParallelFinder.exe
CloseApplications=yes
RestartApplications=no
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Parallel Finder"; Filename: "{app}\ParallelFinder.exe"
[Run]
Filename: "{app}\ParallelFinder.exe"; Description: "Launch Parallel Finder"; Flags: nowait postinstall skipifsilent
