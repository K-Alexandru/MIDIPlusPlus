# Builds the shell and packages it for testers.
#
# The release notes tell testers they can rebuild the binary themselves from the
# named commit, so the packaging has to be a script rather than a set of steps
# somebody remembers. Running this on a clean checkout of the same commit should
# produce the same folder; the zip's hash will differ because Compress-Archive
# stores timestamps.
#
#   & .\tools\make-release.ps1
#   & .\tools\make-release.ps1 -SkipBuild        # package what is already built
#
# Output: build\release\MIDIPlusPlus\ and build\release\MIDIPlusPlus-test-build.zip
#
# Uploading is deliberately not part of this. Publishing is a decision, not a
# build step, and the repo it goes to has changed once already.

param([switch] $SkipBuild)

$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $repoPath 'build\release\MIDIPlusPlus'
$zip = Join-Path $repoPath 'build\release\MIDIPlusPlus-test-build.zip'

if (-not $SkipBuild) {
    $msbuildPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    & $msbuildPath (Join-Path $repoPath 'ui\MIDIShell.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Shell build failed.' }
}

$built = Join-Path $repoPath 'build\shell'
foreach ($required in @('MIDIShell.exe', 'config.json', 'LICENSE', 'IBM-Plex-LICENSE.txt', 'ImGui-LICENSE.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $built $required))) {
        throw "$required is missing from $built. Build without -SkipBuild."
    }
}

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'midi') -Force | Out-Null

# The exe, its config, and the licences the GPL and the two third-party ones
# require to travel with the binary.
foreach ($file in @('MIDIShell.exe', 'config.json', 'LICENSE', 'IBM-Plex-LICENSE.txt', 'ImGui-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $built $file) -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release-README.txt') -Destination (Join-Path $stage 'README.txt')

# Compress-Archive drops empty directories, and the README tells testers to put
# their files in midi\, so the folder needs something in it to survive the zip.
@'
Put your .mid files in this folder, then press Refresh in the app.

Sub-folders are searched too, so you can drop a whole library in here.
You can also point the app at any other folder with the folder button.
'@ | Out-File -LiteralPath (Join-Path $stage 'midi\PUT-MIDI-FILES-HERE.txt') -Encoding utf8

if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -Force

# A binary with no Visual C++ redistributable is the whole reason testers need
# nothing installed, so it is worth failing loudly if that ever changes.
$imports = $null
$toolset = Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC' -Directory -ErrorAction SilentlyContinue |
           Select-Object -Last 1
if ($toolset) { $imports = Join-Path $toolset.FullName 'bin\Hostx64\x64\dumpbin.exe' }
if ($imports -and (Test-Path -LiteralPath $imports)) {
    $dependents = & $imports /dependents (Join-Path $stage 'MIDIShell.exe') 2>&1 | Select-String -Pattern '\.dll'
    $runtime = $dependents | Where-Object { $_ -match 'VCRUNTIME|MSVCP|api-ms-win-crt' }
    if ($runtime) { throw "MIDIShell.exe now needs the VC++ runtime: $runtime. Check RuntimeLibrary is still MultiThreaded." }
}

$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
$commit = (& git -C $repoPath rev-parse --short HEAD).Trim()
Write-Host ""
Write-Host "Staged   $stage"
Write-Host ("Zip      {0}  ({1:N0} bytes)" -f $zip, (Get-Item $zip).Length)
Write-Host "Commit   $commit"
Write-Host "SHA256   $hash"
Write-Host ""
Write-Host "To publish, with the commit and hash above in the notes:"
Write-Host "  gh release create vX.Y.Z-test `"$zip`" --repo <owner>/<repo> --title '...' --notes-file <notes>"
