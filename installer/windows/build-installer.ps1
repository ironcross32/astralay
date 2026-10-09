# Packages an existing Astralay build into a Windows installer.
# Usage: installer\windows\build-installer.ps1 [-BuildDir <dir>] [-Config <config>]
#   BuildDir defaults to <repo>\build, Config to Release.
# Writes <BuildDir>\installer\Astralay-<version>-Windows.exe.
#
# This only packages: build first, so that every format is present. It needs Inno Setup 6.3 or
# later. The installer is unsigned, so SmartScreen warns about a downloaded copy until the user
# chooses to run it anyway.

param(
    [string] $BuildDir,
    [string] $Config = "Release"
)

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build" }
if (-not (Test-Path $BuildDir)) { throw "build-installer: no build folder at $BuildDir" }
$BuildDir = (Resolve-Path $BuildDir).Path
$artefacts = Join-Path $BuildDir "Astralay_artefacts\$Config"

if (-not (Test-Path $artefacts)) { throw "build-installer: no build found at $artefacts" }

# Check everything before packaging anything: a format left out by an interrupted build would
# otherwise only show up on the user's machine.
$vst3 = Join-Path $artefacts "VST3\Astralay.vst3\Contents\x86_64-win\Astralay.vst3"
$clap = Join-Path $artefacts "CLAP\Astralay.clap"
$standalone = Join-Path $artefacts "Standalone\Astralay.exe"

foreach ($file in $vst3, $clap, $standalone) {
    if (-not (Test-Path $file -PathType Leaf)) {
        throw "build-installer: $file is missing; build every format first"
    }
}

# The CLAP carries no version resource, so the version comes from the other two.
$version = (Get-Item $vst3).VersionInfo.ProductVersion
$standaloneVersion = (Get-Item $standalone).VersionInfo.ProductVersion
if (-not $version) { throw "build-installer: can't read a version from $vst3" }
if ($standaloneVersion -ne $version) {
    throw "build-installer: the standalone is version $standaloneVersion, the VST3 is $version"
}

$iscc = Get-Command iscc -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if (-not $iscc) {
    $iscc = ${env:ProgramFiles(x86)}, $env:ProgramFiles |
        Where-Object { $_ } |
        ForEach-Object { Join-Path $_ "Inno Setup 6\ISCC.exe" } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
}
if (-not $iscc) { throw "build-installer: can't find ISCC.exe; install Inno Setup 6" }

$outDir = Join-Path $BuildDir "installer"
$out = Join-Path $outDir "Astralay-$version-Windows.exe"
$work = Join-Path $outDir "work"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null

# Display the full licence for distributed builds. Other notices are installed separately.
$license = Get-Content -Raw -Encoding UTF8 (Join-Path $repo "LICENSE-AGPL-3.0.txt")
$licenseFile = Join-Path $work "License.txt"
# UTF-8 with a byte order mark, which is how Setup tells it from the system code page.
[System.IO.File]::WriteAllText($licenseFile, $license, (New-Object System.Text.UTF8Encoding $true))

if (Test-Path $out) { Remove-Item -Force $out }

& $iscc /Qp `
    "/DAppVersion=$version" `
    "/DArtefacts=$artefacts" `
    "/DRepoDir=$repo" `
    "/DLicenseFile=$licenseFile" `
    "/DOutputDir=$outDir" `
    (Join-Path $here "Astralay.iss")
if ($LASTEXITCODE -ne 0) { throw "build-installer: the Inno Setup compiler failed" }

Remove-Item -Recurse -Force $work

if (-not (Test-Path $out)) { throw "build-installer: the compiler didn't write $out" }
Write-Host "build-installer: wrote $out"
