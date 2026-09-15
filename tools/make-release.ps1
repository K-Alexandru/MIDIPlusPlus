# Builds the shell and packages it for testers, with the audio converter beside it.
#
# The release notes tell testers they can rebuild the binary themselves from the
# named commit, so the packaging has to be a script rather than a set of steps
# somebody remembers. Running this on a clean checkout of the same commit should
# produce the same folder; the zip's hash will differ because the archive
# stores timestamps.
#
#   & .\tools\make-release.ps1
#   & .\tools\make-release.ps1 -SkipBuild        # package what is already built
#   & .\tools\make-release.ps1 -Python C:\py312\python.exe
#
# Output: build\release\MIDIPlusPlus\ and build\release\MIDIPlusPlus-test-build.zip
#
# The converter is an embeddable Python 3.12 with the packages pinned in
# tools\mp3-to-midi\requirements.txt, plus FFmpeg and Deno, every one fetched
# from its publisher at a pinned version and checked against a pinned SHA256.
# Downloads are kept in build\release\downloads, so a rerun fetches nothing.
# The wheels are installed by a Python 3.12 on this machine (the py launcher's,
# or -Python) because the embeddable build has no pip; the wheels are the same
# files either way, and nothing of the host Python ends up in the bundle.
#
# Uploading is deliberately not part of this. Publishing is a decision, not a
# build step, and the repo it goes to has changed once already.

param([switch] $SkipBuild, [string] $Python)

$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $repoPath 'build\release\MIDIPlusPlus'
$zip = Join-Path $repoPath 'build\release\MIDIPlusPlus-test-build.zip'
$downloads = Join-Path $repoPath 'build\release\downloads'
$converterSource = Join-Path $PSScriptRoot 'mp3-to-midi'

# Pinned third-party pieces. Bump a version by changing the URL and the hash
# together; a hash that no longer matches stops the build rather than shipping
# whatever the server sent.
$pythonEmbed = @{ Url = 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip'
                  Sha256 = '4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3' }
$ffmpegBuild = @{ Url = 'https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.1-essentials_build.zip'
                  Sha256 = 'FEC81AE03971D9DD4BE3EBE02E263BD2EC1D789483F931BDBA5F5715E65DA2E9' }
$denoBuild = @{ Url = 'https://github.com/denoland/deno/releases/download/v2.9.6/deno-x86_64-pc-windows-msvc.zip'
                Sha256 = '15E5300B0BA3C3695A7621D90160A746EC9E710228CEE639AFA9D580F6E3CD11' }
$denoLicence = @{ Url = 'https://raw.githubusercontent.com/denoland/deno/v2.9.6/LICENSE.md'
                  Sha256 = 'F62497FFFECC0852960C8D3E6934B9DB86D16396E9B604072E923892CAE3A588'
                  Name = 'deno-2.9.6-LICENSE.md' }

$msbuildPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$vcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC'

function Get-Pinned([hashtable] $Pin) {
    $name = if ($Pin.Name) { $Pin.Name } else { Split-Path -Leaf $Pin.Url }
    $file = Join-Path $downloads $name
    if (-not (Test-Path -LiteralPath $file)) {
        Write-Host "Fetching $($Pin.Url)"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $client = New-Object System.Net.WebClient
        try { $client.DownloadFile($Pin.Url, "$file.partial") } finally { $client.Dispose() }
        Move-Item -LiteralPath "$file.partial" -Destination $file
    }
    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    if ($actual -ne $Pin.Sha256) {
        Remove-Item -LiteralPath $file -Force
        throw "$name does not match its pinned SHA256 (got $actual). Downloaded again next run; if it still differs, the publisher changed the file."
    }
    return $file
}

# One entry of a zip to a path, or every entry into a folder.
function Expand-Entry([string] $Zip, [string] $EntryPattern, [string] $Destination) {
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $entry = $archive.Entries | Where-Object { $_.FullName -like $EntryPattern } | Select-Object -First 1
        if (-not $entry) { throw "$EntryPattern is not in $Zip." }
        New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $Destination, $true)
    } finally { $archive.Dispose() }
}

function Get-FolderSize([string] $Path) {
    (Get-ChildItem -LiteralPath $Path -Recurse -File | Measure-Object -Property Length -Sum).Sum
}

# Runs a native command and returns its output and stderr as lines, leaving
# the exit code in $LASTEXITCODE. Under $ErrorActionPreference = 'Stop',
# PowerShell 5.1 turns a redirected stderr line into a terminating error, so
# the preference is relaxed for the call and the exit code is what is judged.
function Invoke-Capture([scriptblock] $Command) {
    $ErrorActionPreference = 'Continue'
    try { $lines = @(& $Command 2>&1 | ForEach-Object { "$_" }) } finally { $ErrorActionPreference = 'Stop' }
    return ,$lines
}

function Invoke-HostPython { & $hostPython[0] @($hostPython | Select-Object -Skip 1) @args }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# The wheels are fetched by a Python 3.12 of this machine. The version has to
# match the embedded interpreter exactly: pip picks wheels for the Python that
# runs it, and a cp313 torch would not import under python312.dll.
$hostPython = @()
if ($Python) { $hostPython = @($Python) }
elseif (Get-Command py -ErrorAction SilentlyContinue) { $hostPython = @('py', '-3.12') }
else { throw 'No Python 3.12 to install the converter packages with. Install one, or pass -Python <python.exe>.' }
# No double quotes in an inline -c: PowerShell 5.1 drops them on the way to a
# native command.
$hostVersion = Invoke-Capture { Invoke-HostPython -c 'import sys; print(sys.version_info[0], sys.version_info[1])' }
if ($LASTEXITCODE -ne 0 -or "$hostVersion".Trim() -ne '3 12') {
    throw "The Python at '$($hostPython -join ' ')' reports '$hostVersion', and the bundle embeds 3.12. Pass -Python with a 3.12."
}

if (-not $SkipBuild) {
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
New-Item -ItemType Directory -Path $downloads -Force | Out-Null

# The exe, its config, and the licences the GPL and the two third-party ones
# require to travel with the binary.
foreach ($file in @('MIDIShell.exe', 'config.json', 'LICENSE', 'IBM-Plex-LICENSE.txt', 'ImGui-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $built $file) -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release-README.txt') -Destination (Join-Path $stage 'README.txt')

# ---- The converter: converter\ beside the exe, laid out as AudioToMidi.hpp's
# FindInstall expects it, python\python.exe beside convert.py.
$converter = Join-Path $stage 'converter'
$pythonHome = Join-Path $converter 'python'
$sitePackages = Join-Path $pythonHome 'Lib\site-packages'
$licences = Join-Path $converter 'licenses'
New-Item -ItemType Directory -Path $sitePackages, $licences -Force | Out-Null

# Only the two scripts. cookies.txt and browser\ beside them in the source
# tree are the owner's signed-in YouTube session and must never ship.
foreach ($script in @('convert.py', 'signin.py')) {
    Copy-Item -LiteralPath (Join-Path $converterSource $script) -Destination $converter
}

# Embeddable Python. Its ._pth file is the whole import path: the stdlib zip,
# the folder itself, and the site-packages the wheels go into. "import site"
# makes the .pth files in site-packages take effect and keeps the user's own
# site-packages, PYTHONPATH and PYTHONHOME out, so a tester's Python cannot
# leak in.
$embedZip = Get-Pinned $pythonEmbed
[System.IO.Compression.ZipFile]::ExtractToDirectory($embedZip, $pythonHome)
Set-Content -LiteralPath (Join-Path $pythonHome 'python312._pth') -Encoding Ascii -Value @(
    'python312.zip', '.', 'Lib\site-packages', 'import site')
Move-Item -LiteralPath (Join-Path $pythonHome 'LICENSE.txt') -Destination (Join-Path $licences 'Python-3.12.10-LICENSE.txt')

# The Visual C++ runtime. The embeddable build ships vcruntime140 but torch
# also imports msvcp140, msvcp140_atomic_wait and vcruntime140_threads, and
# a machine that has never installed the redistributable would fail at
# "import torch". Beside python.exe they load first, wherever the tester's
# copies are; the sweep further down proves nothing else is missing.
$redist = Get-ChildItem (Join-Path $vcRoot 'Redist\MSVC') -Directory | Where-Object Name -match '^\d' | Select-Object -Last 1
if (-not $redist) { throw "No VC++ redistributable under $vcRoot\Redist\MSVC to bundle beside the converter's python.exe." }
foreach ($folder in @('x64\Microsoft.VC143.CRT', 'x64\Microsoft.VC143.OpenMP')) {
    Copy-Item -Path (Join-Path $redist.FullName "$folder\*.dll") -Destination $pythonHome
}

# The packages, and only the packages: no dependency resolution, no source
# builds, no console-script launchers (they would point at the host Python).
# proxy_tools publishes no wheel at all; it is pure Python, so pip builds one
# without a compiler, and it is the one exception to the wheels-only rule.
Write-Host 'Installing the converter packages'
Invoke-HostPython -m pip install --no-deps --only-binary=:all: --no-binary=proxy_tools --target $sitePackages `
    --requirement (Join-Path $converterSource 'requirements.txt') --disable-pip-version-check --progress-bar off
if ($LASTEXITCODE -ne 0) { throw 'Installing the converter packages failed.' }
Remove-Item -LiteralPath (Join-Path $sitePackages 'bin') -Recurse -Force -ErrorAction SilentlyContinue

# Each package's licence, gathered where a reader can find them; the copies
# inside site-packages stay where pip put them.
foreach ($info in Get-ChildItem -LiteralPath $sitePackages -Directory -Filter '*.dist-info') {
    $package = $info.Name -replace '\.dist-info$', ''
    foreach ($file in Get-ChildItem -LiteralPath $info.FullName -Recurse -File | Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING|NOTICE|AUTHORS)' }) {
        $relative = $file.FullName.Substring($info.FullName.Length + 1) -replace '^licenses\\', ''
        $target = Join-Path (Join-Path $licences $package) $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}

# FFmpeg, one exe of the three in the archive, and Deno, both found by
# convert.py in their folders beside it.
$ffmpegZip = Get-Pinned $ffmpegBuild
Expand-Entry $ffmpegZip '*/bin/ffmpeg.exe' (Join-Path $converter 'ffmpeg\ffmpeg.exe')
Expand-Entry $ffmpegZip '*/LICENSE' (Join-Path $licences 'FFmpeg-9.0.1-LICENSE.txt')
Expand-Entry (Get-Pinned $denoBuild) 'deno.exe' (Join-Path $converter 'deno\deno.exe')
Copy-Item -LiteralPath (Get-Pinned $denoLicence) -Destination (Join-Path $licences 'Deno-2.9.6-LICENSE.md')

# ---- Checks on what was staged.

# A binary with no Visual C++ redistributable is the whole reason testers need
# nothing installed, so it is worth failing loudly if that ever changes.
$dumpbin = $null
$toolset = Get-ChildItem (Join-Path $vcRoot 'Tools\MSVC') -Directory -ErrorAction SilentlyContinue | Select-Object -Last 1
if ($toolset) { $dumpbin = Join-Path $toolset.FullName 'bin\Hostx64\x64\dumpbin.exe' }
if ($dumpbin -and (Test-Path -LiteralPath $dumpbin)) {
    $dependents = & $dumpbin /dependents (Join-Path $stage 'MIDIShell.exe') 2>&1 | Select-String -Pattern '\.dll'
    $runtime = $dependents | Where-Object { $_ -match 'VCRUNTIME|MSVCP|api-ms-win-crt' }
    if ($runtime) { throw "MIDIShell.exe now needs the VC++ runtime: $runtime. Check RuntimeLibrary is still MultiThreaded." }

    # The same promise for the converter: every VC++ runtime DLL any of its
    # binaries imports must be beside python.exe, not left to the tester's
    # machine. The wheels change with each pin, so this is checked, not assumed.
    $shipped = @{}
    foreach ($dll in Get-ChildItem -LiteralPath $pythonHome -File -Filter '*.dll') { $shipped[$dll.Name.ToLowerInvariant()] = $true }
    $missing = @{}
    # Filtered by extension rather than -Include, which PowerShell 5.1 applies
    # unreliably with -LiteralPath and then hands dumpbin every header too.
    foreach ($binary in Get-ChildItem -LiteralPath $pythonHome -Recurse -File | Where-Object { $_.Extension -in '.dll', '.pyd' }) {
        $names = & $dumpbin /dependents $binary.FullName 2>&1 | Select-String -Pattern '^\s+((MSVCP|VCRUNTIME|VCOMP|CONCRT)\w*\.dll)' |
                 ForEach-Object { $_.Matches[0].Groups[1].Value.ToLowerInvariant() }
        foreach ($name in $names) { if (-not $shipped[$name]) { $missing[$name] = $binary.Name } }
    }
    if ($missing.Count) {
        throw "The converter imports VC++ runtime DLLs that are not beside its python.exe: $(($missing.Keys | Sort-Object) -join ', ') (first seen in $($missing.Values | Select-Object -First 1))."
    }
} else {
    Write-Warning 'dumpbin.exe not found; the VC++ runtime checks were skipped.'
}

# The bundle on a machine with no Python: PATH cut to Windows itself, every
# Python variable cleared, and a real conversion of a clip FFmpeg makes.
Write-Host 'Checking the converter with no Python on PATH'
$saved = @{ PATH = $env:PATH; PYTHONHOME = $env:PYTHONHOME; PYTHONPATH = $env:PYTHONPATH; MIDIPP_CONVERTER_PYTHON = $env:MIDIPP_CONVERTER_PYTHON }
$probe = Join-Path $repoPath 'build\release\probe'
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:PYTHONHOME = $null; $env:PYTHONPATH = $null; $env:MIDIPP_CONVERTER_PYTHON = $null
    if (Test-Path -LiteralPath $probe) { Remove-Item -LiteralPath $probe -Recurse -Force }
    New-Item -ItemType Directory -Path $probe -Force | Out-Null
    $stagedPython = Join-Path $pythonHome 'python.exe'
    $importCheck = Join-Path $probe 'check.py'
    Set-Content -LiteralPath $importCheck -Encoding Ascii -Value @'
import ctypes, sys
import torch, transkun.transcribe, yt_dlp, yt_dlp_ejs, webview, webview.platforms.winforms
# msvcp140 must have come from beside python.exe, not from the machine.
kernel32 = ctypes.windll.kernel32
kernel32.GetModuleHandleW.restype = ctypes.c_void_p
kernel32.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint]
path = ctypes.create_unicode_buffer(1024)
kernel32.GetModuleFileNameW(kernel32.GetModuleHandleW("msvcp140.dll"), path, 1024)
print("torch", torch.__version__, "runtime", path.value)
'@
    $report = Invoke-Capture { & $stagedPython $importCheck }
    if ($LASTEXITCODE -ne 0) { throw "The staged converter does not import on a bare PATH:`n$($report -join "`n")" }
    $summary = $report | Where-Object { $_ -like 'torch *' } | Select-Object -Last 1
    if ("$summary" -notlike "*runtime $pythonHome\*") { throw "The staged converter loaded the VC++ runtime from outside the bundle:`n$($report -join "`n")" }
    Write-Host "  $summary"

    $clip = Join-Path $probe 'clip.wav'
    & (Join-Path $converter 'ffmpeg\ffmpeg.exe') -v error -y -f lavfi -i 'sine=frequency=262:duration=1.5,volume=0.4' `
        -f lavfi -i 'sine=frequency=330:duration=1.5,volume=0.4' -filter_complex '[0][1]concat=n=2:v=0:a=1' -ar 16000 -ac 1 $clip
    if ($LASTEXITCODE -ne 0) { throw 'The bundled FFmpeg could not write a test clip.' }
    $convertScript = Join-Path $converter 'convert.py'
    $lines = Invoke-Capture { & $stagedPython -u $convertScript $clip --out-dir $probe --device cpu }
    $done = $lines | Where-Object { $_ -like 'done: *' } | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0 -or -not $done -or -not (Test-Path -LiteralPath $done.Substring(6))) {
        throw "The staged converter did not convert a clip:`n$($lines -join "`n")"
    }
    Write-Host "  $done"
} finally {
    $env:PATH = $saved.PATH; $env:PYTHONHOME = $saved.PYTHONHOME; $env:PYTHONPATH = $saved.PYTHONPATH
    $env:MIDIPP_CONVERTER_PYTHON = $saved.MIDIPP_CONVERTER_PYTHON
    if (Test-Path -LiteralPath $probe) { Remove-Item -LiteralPath $probe -Recurse -Force -ErrorAction SilentlyContinue }
}

# ---- The zip.
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }

# Written entry by entry rather than with Compress-Archive, which silently drops
# empty directories. The README tells testers to drop their files in midi\, so
# that folder has to arrive, and it should arrive empty rather than carrying a
# placeholder file explaining why it is not empty.
Write-Host 'Writing the zip'
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName) {
        $relative = $file.FullName.Substring($stage.Length + 1) -replace '\\', '/'
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, "MIDIPlusPlus/$relative",
            [System.IO.Compression.CompressionLevel]::Optimal)
    }
    # A trailing slash is what makes this a directory entry rather than a file.
    [void]$archive.CreateEntry('MIDIPlusPlus/midi/')
} finally { $archive.Dispose() }

$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
$commit = (& git -C $repoPath rev-parse --short HEAD).Trim()
Write-Host ""
Write-Host ("Staged     {0}  ({1:N0} MB, converter {2:N0} MB)" -f $stage, ((Get-FolderSize $stage) / 1MB), ((Get-FolderSize $converter) / 1MB))
Write-Host ("Zip        {0}  ({1:N0} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
Write-Host "Commit     $commit"
Write-Host "SHA256     $hash"
Write-Host ""
Write-Host "To publish, with the commit and hash above in the notes:"
Write-Host "  gh release create vX.Y.Z-test `"$zip`" --repo <owner>/<repo> --title '...' --notes-file <notes>"
