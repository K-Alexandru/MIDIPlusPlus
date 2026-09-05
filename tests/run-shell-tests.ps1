param([switch]$Render)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$msbuildPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuildPath (Join-Path $PSScriptRoot 'ShellTests.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Shell test build failed.' }
$testDirectory = Join-Path $repoPath 'build\shell-tests'
Copy-Item -LiteralPath (Join-Path $repoPath 'x64\Release\config.json') -Destination (Join-Path $testDirectory 'config.json')
Push-Location -LiteralPath $testDirectory
try {
    $ErrorActionPreference = 'Continue'
    & '.\ShellTests.exe'
    $testExitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($testExitCode -ne 0) { throw 'Shell tests failed.' }
} finally { Pop-Location }
if ($Render) {
    & $msbuildPath (Join-Path $PSScriptRoot 'RenderTests.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Render test build failed.' }
    $renderDirectory = Join-Path $repoPath 'build\render-tests'
    Copy-Item -LiteralPath (Join-Path $repoPath 'x64\Release\config.json') -Destination (Join-Path $renderDirectory 'config.json')
    Push-Location -LiteralPath $renderDirectory
    try {
        & '.\RenderTests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Render tests failed.' }
    } finally { Pop-Location }
}
