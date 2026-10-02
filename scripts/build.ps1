[CmdletBinding()]
param(
    [switch]$WithSdk,
    [string]$SdkDir = '',
    [switch]$SkipTests,
    [ValidateSet('Release', 'RelWithDebInfo')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$deckSource = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'dev-shell.ps1')
$deckBuild = Join-Path $deckSource $(if ($WithSdk) { 'build-sdk' } else { 'build' })
$deckArgs = @('-S', $deckSource, '-B', $deckBuild, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Configuration", "-DBUILD_TESTING=$(if ($SkipTests) { 'OFF' } else { 'ON' })")
if ($WithSdk) {
    if (-not (Test-Path -LiteralPath (Join-Path $SdkDir 'Win\Include\DeckLinkAPI.idl'))) { throw 'SDK must contain Win/Include/DeckLinkAPI.idl.' }
    $deckArgs += @('-DVTRPON_DECKLINK_ENABLE_SDK=ON', "-DVTRPON_DECKLINK_SDK_DIR=$SdkDir")
} else { $deckArgs += '-DVTRPON_DECKLINK_ENABLE_SDK=OFF' }
& cmake @deckArgs
if ($LASTEXITCODE -ne 0) { throw 'DeckLink CMake configure failed.' }
& cmake --build $deckBuild
if ($LASTEXITCODE -ne 0) { throw 'DeckLink build failed.' }
if (-not $SkipTests) {
    & ctest --test-dir $deckBuild --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'DeckLink tests failed.' }
}
