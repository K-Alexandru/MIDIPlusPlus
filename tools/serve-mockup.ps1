<#
.SYNOPSIS
  Serve skin-system.html over http://127.0.0.1 so it can be opened in a browser
  that refuses local file: URLs.

.DESCRIPTION
  The mockup is the UI spec and it is operable, not a picture, so reading its
  source is not the same as using it. Assistant browser tooling rejects file:
  URLs, which is what blocked the rendered comparison recorded in
  CONTINUE-HERE.md. This serves the repository root read-only on the loopback
  interface and prints the deep links worth opening first.

  Raw TcpListener rather than HttpListener: HttpListener needs a URL ACL
  reservation and fails with access denied for a non-elevated user, and
  installing a reservation to look at a mockup is not a trade worth making.
  There is no Python or Node on this machine to fall back to.

.EXAMPLE
  .\tools\serve-mockup.ps1
  .\tools\serve-mockup.ps1 -Port 9000
#>
[CmdletBinding()]
param(
  [int]$Port = 8756,
  [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path

$types = @{
  '.html' = 'text/html; charset=utf-8'
  '.css'  = 'text/css; charset=utf-8'
  '.js'   = 'text/javascript; charset=utf-8'
  '.json' = 'application/json; charset=utf-8'
  '.svg'  = 'image/svg+xml'
  '.png'  = 'image/png'
  '.md'   = 'text/plain; charset=utf-8'
  '.woff2'= 'font/woff2'
  '.ico'  = 'image/x-icon'
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
try { $listener.Start() }
catch { throw "Could not listen on port ${Port}: $($_.Exception.Message)" }

$base = "http://127.0.0.1:$Port"
Write-Host "Serving $Root at $base  (Ctrl+C to stop)"
Write-Host ""
Write-Host "  $base/skin-system.html"
Write-Host "  $base/skin-system.html?skin=modern-dark&mode=full&keys=0"
Write-Host "  $base/skin-system.html?skin=classic&mode=live&frame=1"
Write-Host "  $base/skin-system.html?skin=modern&mode=full&keys=1&only=keys&frame=1"
Write-Host ""
Write-Host "  skin  classic | classic-dark | modern | modern-dark"
Write-Host "  mode  full | live | auto"
Write-Host "  keys  1 shows the key mapping window, 0 hides it (full mode only)"
Write-Host "  frame 1 hides the page prose, leaving the window alone"
Write-Host "  only  keys captures the key mapping window without the main one"
Write-Host ""

function Send-Response {
  param($Stream, [int]$Status, [string]$Reason, [string]$Type, [byte[]]$Body, [bool]$HeadOnly)
  $length = if ($null -eq $Body) { 0 } else { $Body.Length }
  $head = "HTTP/1.1 $Status $Reason`r`n" +
          "Content-Type: $Type`r`n" +
          "Content-Length: $length`r`n" +
          "Cache-Control: no-store`r`n" +
          "Connection: close`r`n`r`n"
  $headBytes = [System.Text.Encoding]::ASCII.GetBytes($head)
  $Stream.Write($headBytes, 0, $headBytes.Length)
  if (-not $HeadOnly -and $length -gt 0) { $Stream.Write($Body, 0, $length) }
  $Stream.Flush()
}

try {
  while ($true) {
    $client = $listener.AcceptTcpClient()
    try {
      $client.ReceiveTimeout = 5000
      $stream = $client.GetStream()
      $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII, $false, 1024, $true)
      $request = $reader.ReadLine()
      if (-not $request) { continue }
      # Drain the headers. The body is ignored: this server only reads.
      while ($true) { $line = $reader.ReadLine(); if ($null -eq $line -or $line -eq '') { break } }

      $parts = $request.Split(' ')
      $method = $parts[0]
      $target = if ($parts.Count -gt 1) { $parts[1] } else { '/' }
      $headOnly = ($method -eq 'HEAD')

      if ($method -ne 'GET' -and -not $headOnly) {
        Send-Response $stream 405 'Method Not Allowed' 'text/plain; charset=utf-8' ([System.Text.Encoding]::UTF8.GetBytes('Read only.')) $false
        continue
      }

      $path = ($target -split '\?')[0]
      $path = [System.Uri]::UnescapeDataString($path)
      if ($path -eq '/' -or $path -eq '') { $path = '/skin-system.html' }
      $relative = $path.TrimStart('/').Replace('/', '\')

      $full = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($Root, $relative))
      # Anything resolving outside the root is refused rather than clamped, so a
      # traversal attempt is visible in the log instead of silently succeeding.
      $inside = $full.StartsWith($Root + '\', [StringComparison]::OrdinalIgnoreCase)

      if (-not $inside -or -not (Test-Path -LiteralPath $full -PathType Leaf)) {
        Write-Host ("404  " + $path)
        Send-Response $stream 404 'Not Found' 'text/plain; charset=utf-8' ([System.Text.Encoding]::UTF8.GetBytes('Not found.')) $headOnly
        continue
      }

      $ext = [System.IO.Path]::GetExtension($full).ToLowerInvariant()
      $type = if ($types.ContainsKey($ext)) { $types[$ext] } else { 'application/octet-stream' }
      $bytes = [System.IO.File]::ReadAllBytes($full)
      Write-Host ("200  " + $path + "  " + $bytes.Length + " bytes")
      Send-Response $stream 200 'OK' $type $bytes $headOnly
    }
    catch { Write-Host ("error  " + $_.Exception.Message) }
    finally { $client.Close() }
  }
}
finally { $listener.Stop() }
