; Astralay's Windows installer. Don't compile this directly: build-installer.ps1 checks the build
; and runs the compiler, passing in AppVersion, Artefacts, RepoDir, LicenseFile and OutputDir.

#ifndef AppVersion
  #error Run installer\windows\build-installer.ps1 instead of compiling this file directly
#endif

[Setup]
; Identifies Astralay to Windows, so that a later release upgrades this one. Never change it.
AppId={{7C0B6D4E-3A52-4F1B-9E87-5D2A1C94B6F3}
AppName=Astralay
AppVersion={#AppVersion}
AppVerName=Astralay {#AppVersion}
AppPublisher=Iron Labs
AppPublisherURL=https://github.com/ironcross32/astralay
VersionInfoVersion={#AppVersion}

; The plugin folders under Common Files, for every user of the PC.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; The plugins have fixed folders. This one holds the standalone, the licences and the uninstaller.
DefaultDirName={autopf}\Astralay
DisableDirPage=yes
DisableProgramGroupPage=yes

; Show AGPLv3 for distributed builds; install the other licence notices separately.
LicenseFile={#LicenseFile}

OutputDir={#OutputDir}
OutputBaseFilename=Astralay-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "clap"; Description: "CLAP plugin"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full custom

[InstallDelete]
; Setup merges into a bundle that's already there, so a file dropped from a later release would
; linger. Remove the old copy first.
Type: filesandordirs; Name: "{commoncf64}\VST3\Astralay.vst3"; Components: vst3

[Files]
; ignoreversion so that an older release can be installed over a newer one.
Source: "{#Artefacts}\VST3\Astralay.vst3\*"; DestDir: "{commoncf64}\VST3\Astralay.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\CLAP\Astralay.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#Artefacts}\Standalone\Astralay.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#RepoDir}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#RepoDir}\LICENSE-AGPL-3.0.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoDir}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Astralay"; Filename: "{app}\Astralay.exe"; Components: standalone
