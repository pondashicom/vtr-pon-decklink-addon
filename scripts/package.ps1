param([string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build-release'), [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\packages'))
$ErrorActionPreference = 'Stop'
& py -3 (Join-Path $PSScriptRoot 'package-release.py') --build-dir $BuildDirectory --output-dir $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Package generation failed.' }
