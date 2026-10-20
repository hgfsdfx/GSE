param(
    [string]$State,
    [ValidateRange(1, 65535)]
    [int]$Port = 8765,
    [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'
$monitorRoot = Split-Path -Parent $PSScriptRoot
$monitorUrl = "http://127.0.0.1:$Port/"
$monitorReady = $false

try
{
    $monitorResponse = Invoke-WebRequest -Uri $monitorUrl -UseBasicParsing -TimeoutSec 2
    $monitorReady = $monitorResponse.StatusCode -eq 200 -and $monitorResponse.Content.Contains('dashboard.js')
    if (-not $monitorReady)
    {
        throw "Port $Port is being used by a different server. Choose another -Port."
    }
}
catch
{
    if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
    {
        throw "Port $Port is already in use. Choose another -Port or stop that server first."
    }
}

if (-not $monitorReady)
{
    $monitorBundledPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    $monitorPython = $null
    $monitorArguments = @('-u')
    if (Test-Path -LiteralPath $monitorBundledPython)
    {
        $monitorPython = $monitorBundledPython
    }
    else
    {
        $monitorCommand = Get-Command python -ErrorAction SilentlyContinue
        if ($monitorCommand -and $monitorCommand.Source -notlike '*\Microsoft\WindowsApps\*')
        {
            $monitorPython = $monitorCommand.Source
        }
        else
        {
            $monitorCommand = Get-Command py -ErrorAction SilentlyContinue
            if ($monitorCommand)
            {
                $monitorPython = $monitorCommand.Source
                $monitorArguments = @('-3', '-u')
            }
        }
    }
    if (-not $monitorPython)
    {
        throw 'Python 3.9 or newer is required. No usable Python runtime was found.'
    }
    $monitorArguments += '"' + (Join-Path $PSScriptRoot 'monitor_server.py') + '"'
    $monitorArguments += @('--port', "$Port")
    if ($State)
    {
        $monitorArguments += @('--state', ('"' + [System.IO.Path]::GetFullPath($State) + '"'))
    }
    $monitorLogDirectory = Join-Path $monitorRoot 'monitoring'
    $monitorOutput = Join-Path $monitorLogDirectory "server-$Port.log"
    $monitorError = Join-Path $monitorLogDirectory "server-$Port.error.log"
    $env:PYTHONUTF8 = '1'
    $monitorProcess = Start-Process -FilePath $monitorPython -ArgumentList $monitorArguments -WorkingDirectory $monitorRoot -WindowStyle Hidden -RedirectStandardOutput $monitorOutput -RedirectStandardError $monitorError -PassThru
    for ($monitorAttempt = 0; $monitorAttempt -lt 20; $monitorAttempt++)
    {
        try
        {
            $monitorResponse = Invoke-WebRequest -Uri $monitorUrl -UseBasicParsing -TimeoutSec 1
            $monitorReady = $monitorResponse.StatusCode -eq 200 -and $monitorResponse.Content.Contains('dashboard.js')
            if ($monitorReady) { break }
        }
        catch { }
        if ($monitorProcess.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $monitorReady)
    {
        if (Test-Path -LiteralPath $monitorError) { Get-Content -LiteralPath $monitorError -Tail 10 }
        throw "Monitor did not start. See $monitorError"
    }
    Write-Host "Monitor server PID: $($monitorProcess.Id)"
}
else
{
    Write-Host 'The monitor server is already running. Its current data-source settings are reused.'
}

Write-Host "Monitor: $monitorUrl"
if (-not $NoBrowser) { Start-Process $monitorUrl }
