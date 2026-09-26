param([string]$BuildDir = 'build-release', [string]$QtDir = 'C:/Qt/6.11.2/mingw_64', [string]$Compiler = 'C:/Qt/Tools/mingw1310_64/bin/g++.exe')
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
$buildPath = (Resolve-Path (Join-Path $projectDir $BuildDir)).Path
$env:PATH = (Split-Path $Compiler -Parent) + ';' + $QtDir + '/bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:/Windows/Fonts'
$testObject = Join-Path $buildPath 'text_settings_smoke.obj'
$testExe = Join-Path $buildPath 'text_settings_smoke.exe'
$includes = @('QtCore', 'QtGui', 'QtWidgets', 'QtSql', 'QtNetwork', 'QtSvg', 'QtMultimedia', 'QtMultimediaWidgets')
$compileArgs = @('-std=c++17', '-O1', ('-I' + $projectDir + '/src'), ('-I' + $QtDir + '/include'), ('-I' + $QtDir + '/mkspecs/win32-g++'))
foreach ($module in $includes) { $compileArgs += '-I' + $QtDir + '/include/' + $module }
$compileArgs += @('-c', (Join-Path $PSScriptRoot 'text_settings_smoke.cpp'), '-o', $testObject)
& $Compiler @compileArgs
if ($LASTEXITCODE -ne 0) { throw 'Smoke test compilation failed' }
$linkLine = Get-Content -LiteralPath (Join-Path $buildPath 'build.ninja') | Where-Object { $_ -match '^build Sermon.exe:' }
$objects = @([regex]::Matches($linkLine, '[^\s]+\.obj') | ForEach-Object { $_.Value } | Where-Object { $_ -notmatch '/main\.cpp\.obj$|/app\.rc\.obj$' } | ForEach-Object { Join-Path $buildPath $_ })
$linkArgs = @($testObject) + $objects + @('-o', $testExe, ('-L' + $QtDir + '/lib'))
foreach ($module in $includes) { $linkArgs += '-l' + $module.Replace('Qt', 'Qt6') }
& $Compiler @linkArgs
if ($LASTEXITCODE -ne 0) { throw 'Smoke test linking failed' }
Push-Location $buildPath
try { & $testExe; if ($LASTEXITCODE -ne 0) { throw 'V2 settings smoke checks failed' } }
finally { Pop-Location }
