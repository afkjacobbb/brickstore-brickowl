# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory=$true)][string]$Payload)
$ErrorActionPreference = 'Stop'
$Payload = (Resolve-Path $Payload).Path
# Run from vcvars64, and copy only Microsoft's redistributable (not debug) CRT.
if (!$env:VCToolsRedistDir) { throw 'Run inside the MSVC 2022 x64 environment.' }
$crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
    Sort-Object Name -Descending | Select-Object -First 1
if (!$crt) { throw 'MSVC redistributable CRT directory not found.' }
Copy-Item (Join-Path $crt.FullName '*.dll') $Payload -Force
if (!(Test-Path "$Payload/vc_redist.x64.exe")) {
    $redist = Get-ChildItem $env:VCToolsRedistDir -Recurse -Filter vc_redist.x64.exe | Select-Object -First 1
    if (!$redist) { throw 'Official VC redistributable installer not found.' }
    Copy-Item $redist.FullName $Payload
}
@'
[Paths]
Prefix=.
Plugins=.
QmlImports=qml
'@ | Set-Content "$Payload/qt.conf" -Encoding ascii
foreach ($file in @('BrickStore.exe','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Network.dll',
                    'platforms/qwindows.dll','tls/qschannelbackend.dll','imageformats/qsvg.dll',
                    'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll','vc_redist.x64.exe')) {
    if (!(Test-Path (Join-Path $Payload $file))) { throw "Missing deployment dependency: $file" }
}
# Do not ship build tools, symbols, or CI results in the user-facing portable tree.
Get-ChildItem $Payload -Recurse -Filter '*.pdb' | Remove-Item -Force
Copy-Item "$PSScriptRoot/../COPYING*" $Payload -Force
Copy-Item "$PSScriptRoot/../doc/development/WINDOWS-PROTOTYPE-PL.md" "$Payload/README-BrickOwl-PL.md"
