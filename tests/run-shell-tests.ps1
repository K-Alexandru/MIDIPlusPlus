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
# The coloured page draws with its own script; ShellTests writes the page and
# a fixture, and node holds the script to the app's text. Node is only on a
# developer machine, so its absence is reported, not fatal.
$node = Get-Command node -ErrorAction SilentlyContinue
if ($node) {
    $ErrorActionPreference = 'Continue'
    & $node.Source (Join-Path $PSScriptRoot 'sheet-page-parity.js') (Join-Path $testDirectory 'sheet-page.html') (Join-Path $testDirectory 'sheet-page-parity.json')
    $parityExitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($parityExitCode -ne 0) { throw 'Sheet page parity failed.' }
} else {
    Write-Host 'SKIP sheet page parity: node is not installed.'
}
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
