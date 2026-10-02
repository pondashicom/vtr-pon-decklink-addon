[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$deckVsWhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $deckVsWhere)) { throw 'Visual Studio Installer/vswhere is required.' }
# Request only a path: localized vswhere JSON descriptions can have invalid escapes.
$deckVs = & $deckVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $deckVs) { throw 'MSVC x64 tools are required.' }
Import-Module (Join-Path $deckVs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $deckVs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$env:VSLANG = '1033'
foreach ($deckTool in @('cl', 'midl', 'cmake', 'ninja')) {
    if (-not (Get-Command $deckTool -ErrorAction SilentlyContinue)) { throw "Missing tool: $deckTool" }
}
