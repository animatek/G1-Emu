; The Windows installer: Inno Setup 6, built by CI next to the zip (.github/workflows/build.yml).
;
;   iscc /DAppVersion=v0.1.0-alpha.20 /DPackageDir=..\..\package packaging\windows\G1-Emu.iss
;
; PackageDir is what the zip holds: G1-Emu.exe, g1run.exe, the plugins (G1-Emu, G1-Emu FX and G1-Emu Micro, as
; .vst3 and .clap) and the docs.
; The plugins go where every DAW looks, and a new version replaces the old one in place.
; Copies left by hand in the per-user plugin folders are removed, since a DAW that finds two loads
; whichever it likes, often the old one (#53, #59, #65). What the user made is never touched:
; the ROM, flash.bin with the banks and the settings live in %APPDATA%\Animatek\G1-Emu.

#ifndef AppVersion
  #define AppVersion "dev"
#endif
#ifndef PackageDir
  #define PackageDir "..\..\package"
#endif

[Setup]
; Fixed for good: it is how Windows knows a new version replaces an installed one.
AppId={{EDDF8577-69B6-4C80-AEF6-9A35A5ACB816}
AppName=G1-Emu
AppVersion={#AppVersion}
AppVerName=G1-Emu {#AppVersion}
AppPublisher=Animatek
AppPublisherURL=https://github.com/animatek/G1-Emu
AppSupportURL=https://github.com/animatek/G1-Emu/issues
VersionInfoVersion=0.1.0.0
DefaultDirName={autopf}\G1-Emu
DefaultGroupName=G1-Emu
DisableProgramGroupPage=yes
; Admin, because the plugins go to Common Files, the folder every DAW scans.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; A DAW with the plugin loaded holds its files: offer to close it rather than fail half-way.
CloseApplications=yes
RestartApplications=no
; The per-user plugin folders are only cleaned, never installed into.
UsedUserAreasWarning=no
UninstallDisplayIcon={app}\G1-Emu.exe
UninstallDisplayName=G1-Emu
OutputDir=.
OutputBaseFilename=G1-Emu-windows-x86_64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "standalone"; Description: "G1-Emu, the synth in its own window"; Types: full custom
Name: "vst3"; Description: "VST3 plugins, instrument, FX and Micro (Common Files\VST3)"; Types: full custom
Name: "clap"; Description: "CLAP plugins, instrument, FX and Micro (Common Files\CLAP)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "A desktop shortcut"; Components: standalone; Flags: unchecked

[InstallDelete]
; The previous version, so nothing of it is left mixed with the new one.
Type: filesandordirs; Name: "{commoncf64}\VST3\G1-Emu.vst3"; Components: vst3
Type: files; Name: "{commoncf64}\CLAP\G1-Emu.clap"; Components: clap
Type: filesandordirs; Name: "{commoncf64}\VST3\G1-Emu FX.vst3"; Components: vst3
Type: files; Name: "{commoncf64}\CLAP\G1-Emu FX.clap"; Components: clap
Type: filesandordirs; Name: "{commoncf64}\VST3\G1-Emu Micro.vst3"; Components: vst3
Type: files; Name: "{commoncf64}\CLAP\G1-Emu Micro.clap"; Components: clap
; Copies made by hand where some DAWs also look: they would shadow the installed one.
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\G1-Emu.vst3"; Components: vst3
Type: files; Name: "{localappdata}\Programs\Common\CLAP\G1-Emu.clap"; Components: clap
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\G1-Emu FX.vst3"; Components: vst3
Type: files; Name: "{localappdata}\Programs\Common\CLAP\G1-Emu FX.clap"; Components: clap
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\G1-Emu Micro.vst3"; Components: vst3
Type: files; Name: "{localappdata}\Programs\Common\CLAP\G1-Emu Micro.clap"; Components: clap

[Files]
Source: "{#PackageDir}\G1-Emu.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#PackageDir}\g1run.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#PackageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\WINDOWS.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\G1-Emu.vst3\*"; DestDir: "{commoncf64}\VST3\G1-Emu.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\G1-Emu.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#PackageDir}\G1-Emu FX.vst3\*"; DestDir: "{commoncf64}\VST3\G1-Emu FX.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\G1-Emu FX.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#PackageDir}\G1-Emu Micro.vst3\*"; DestDir: "{commoncf64}\VST3\G1-Emu Micro.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\G1-Emu Micro.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion

[Icons]
Name: "{group}\G1-Emu"; Filename: "{app}\G1-Emu.exe"; Components: standalone
Name: "{group}\Uninstall G1-Emu"; Filename: "{uninstallexe}"
Name: "{autodesktop}\G1-Emu"; Filename: "{app}\G1-Emu.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\G1-Emu.exe"; Description: "Start G1-Emu"; Components: standalone; Flags: nowait postinstall skipifsilent
