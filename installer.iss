[Setup]
AppName=Sermon
AppVersion=1.0
DefaultDirName={pf}\Sermon
DefaultGroupName=Sermon
OutputBaseFilename=Sermon_Setup
Compression=lzma2
SolidCompression=yes
OutputDir=C:\Users\Acer\Desktop\Sermon
PrivilegesRequired=admin

[Files]
Source: "C:\Users\Acer\Desktop\Sermon\deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{commondesktop}\Sermon"; Filename: "{app}\Sermon.exe"
Name: "{group}\Sermon"; Filename: "{app}\Sermon.exe"
Name: "{group}\Uninstall Sermon"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Sermon.exe"; Description: "Launch Sermon"; Flags: nowait postinstall skipifsilent
