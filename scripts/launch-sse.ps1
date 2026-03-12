# Singleton launcher for codebase-memory-mcp in SSE mode.
# Safe to call multiple times — starts the server only if not already running.
# Usage: powershell -ExecutionPolicy Bypass -File launch-sse.ps1

$port        = 6274
$idleTimeout = 30   # minutes
$binary      = "$env:LOCALAPPDATA\codebase-memory-mcp\codebase-memory-mcp.exe"

if (-not (Test-Path $binary)) {
    Write-Error "Binary not found at $binary"
    exit 1
}

$listening = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
if ($listening) {
    Write-Host "codebase-memory-mcp already running on port $port"
    exit 0
}

Write-Host "Starting codebase-memory-mcp SSE server on port $port (idle timeout: ${idleTimeout}m)..."
Start-Process -NoNewWindow -FilePath $binary `
    -ArgumentList "--transport", "sse", "--port", "$port", "--idle-timeout", "$idleTimeout"

# Brief wait, then confirm it came up
Start-Sleep -Milliseconds 500
$listening = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
if ($listening) {
    Write-Host "Server started successfully"
} else {
    Write-Warning "Server may not have started — check for errors"
}
