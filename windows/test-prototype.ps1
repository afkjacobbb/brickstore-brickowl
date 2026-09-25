# SPDX-License-Identifier: GPL-3.0-only
param([string]$Build = 'build')
$ErrorActionPreference = 'Stop'
$Build = (Resolve-Path $Build).Path
$evidence = New-Item -ItemType Directory -Force "$Build/smoke-evidence"
$zip = "$Build/BrickStore-BrickOwl-Windows-x64.zip"
$setup = "$Build/BrickStore-BrickOwl-Setup-x64.exe"
if (!(Test-Path $setup)) { throw 'Installer was not created.' }
Compress-Archive -Path "$Build/bin/*" -DestinationPath $zip -Force
$portable = Join-Path $env:RUNNER_TEMP 'brickowl-portable-test'
$installed = Join-Path $env:RUNNER_TEMP 'brickowl-installed-test'
Expand-Archive $zip $portable -Force
# Deployment must work without Qt or compiler directories on PATH.
$env:PATH = "$env:SystemRoot/System32;$env:SystemRoot;$env:SystemRoot/System32/WindowsPowerShell/v1.0"
foreach ($name in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM',
                    'QML_IMPORT_PATH','QML2_IMPORT_PATH','QTDIR','Qt6_DIR')) {
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}
# Software rendering on hosted machines without a physical GPU. Still use native qwindows.
$env:QSG_RHI_BACKEND = 'software'
$env:QT_QUICK_BACKEND = 'software'
function Test-Application([string]$Directory, [string]$Label) {
    $logs = New-Item -ItemType Directory -Force "$evidence/$Label"
    $process = Start-Process "$Directory/BrickStore.exe" -ArgumentList '--new-instance','--brickowl-smoke-test' `
        -WorkingDirectory $logs.FullName -PassThru `
        -RedirectStandardOutput "$logs/stdout.log" -RedirectStandardError "$logs/stderr.log"
    if (!$process.WaitForExit(90000)) {
        $process.Kill()
        throw "$Label application did not finish its smoke test in 90 seconds."
    }
    if ($process.ExitCode -ne 0) { throw "$Label smoke failed with exit code $($process.ExitCode). See smoke evidence." }
    $report = Get-Content "$logs/brickowl-smoke.json" -Raw | ConvertFrom-Json
    if (!$report.passed -or $report.platform -ne 'windows') { throw "$Label did not pass native Windows checks." }
}
Test-Application $portable 'portable'
# Also install the actual generated artifact and test its deployed tree.
$install = Start-Process $setup -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER',
    "/DIR=`"$installed`"", "/LOG=`"$evidence/install.log`"") -PassThru
if (!$install.WaitForExit(180000)) { $install.Kill(); throw 'Installer timeout.' }
if ($install.ExitCode -ne 0) { throw "Installer failed: $($install.ExitCode)" }
Test-Application $installed 'installed'
$uninstall = Start-Process "$installed/unins000.exe" -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -PassThru
if (!$uninstall.WaitForExit(90000)) { $uninstall.Kill(); throw 'Uninstaller timeout.' }
if ($uninstall.ExitCode -ne 0 -or (Test-Path "$installed/BrickStore.exe")) { throw 'Uninstall verification failed.' }
Get-FileHash $setup,$zip -Algorithm SHA256 | ForEach-Object {
    "$($_.Hash.ToLowerInvariant())  $(Split-Path $_.Path -Leaf)"
} | Set-Content "$Build/SHA256SUMS.txt" -Encoding ascii
