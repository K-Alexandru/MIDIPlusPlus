param([string]$LoopbackPort, [switch]$ListPorts, [switch]$Legit)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$msbuildPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuildPath)) { throw 'Visual Studio 2022 Build Tools MSBuild was not found.' }
& $msbuildPath (Join-Path $PSScriptRoot 'LatencyTests.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Latency test build failed.' }
$testDirectory = Join-Path $repoPath 'build\latency\tests'
Copy-Item -LiteralPath (Join-Path $repoPath 'MIDI++\config.json') -Destination (Join-Path $testDirectory 'config.json')
Push-Location -LiteralPath $testDirectory
try {
    # The engine writes benign notices (for example the inherited MMCSS warning) to
    # stderr. Under Windows PowerShell 5.1 a redirected native stderr line becomes an
    # ErrorRecord, which 'Stop' would turn into a terminating error and abort the run
    # midway. Exit code stays the pass/fail signal.
    $ErrorActionPreference = 'Continue'
    if ($ListPorts) { & '.\LatencyTests.exe' --list }
    elseif ($Legit) { & '.\LatencyTests.exe' --legit }
    elseif ($LoopbackPort) { & '.\LatencyTests.exe' --loopback $LoopbackPort }
    else { & '.\LatencyTests.exe' }
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($exitCode -ne 0) { throw 'Latency tests failed.' }
}
finally { Pop-Location }
