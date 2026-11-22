[CmdletBinding()]
param(
    [string]$ModelEndpoint = $env:REDFLAG_MODEL_ENDPOINT,
    [string]$ModelName = $env:REDFLAG_MODEL_NAME,
    [switch]$Vision,
    [string]$LlamaServer,
    [string]$ModelFile,
    [string]$VisionProjector,
    [string]$SpeechRuntime = $env:REDFLAG_SPEECH_RUNTIME,
    [string]$SpeechPython = $env:REDFLAG_SPEECH_PYTHON,
    [int]$DecisionPort = 8716,
    [int]$SpeechPort = 8717,
    [int]$LlamaPort = 18157,
    [switch]$SkipSpeech,
    [switch]$SkipDecision,
    [switch]$RequireAll
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$cache = Join-Path $project 'tmp/gs'
$portable = Join-Path $project 'CompanionRuntime'
if (Test-Path "$project/RedflagForge.exe") { $env:PATH = "$project;$env:PATH" }
$logs = Join-Path $project 'tmp/npc-services'
$null = New-Item -ItemType Directory -Force $logs

function Read-Health([string]$url) {
    try { Invoke-RestMethod -Uri $url -TimeoutSec 2 -ErrorAction Stop } catch { $null }
}

function Start-LocalProcess([string]$name, [string]$exe, [string[]]$arguments, [string]$health) {
    if (Read-Health $health) { Write-Host "$name ready ($health)"; return }
    $quoted = foreach ($argValue in $arguments) { '"' + $argValue.Replace('"','\"') + '"' }
    $process = Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory $project -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs/$name.out" -RedirectStandardError "$logs/$name.err"
    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline) {
        if ($process.HasExited) { throw "$name exited. See $logs/$name.err" }
        if (Read-Health $health) { Write-Host "$name ready (PID $($process.Id), $health)"; return }
        Start-Sleep -Milliseconds 300
    }
    $process.Kill()
    throw "$name did not start within 90 seconds. See $logs/$name.err"
}

if (!$SpeechPython) {
    $SpeechPython = if (Test-Path "$portable/python/python.exe") { "$portable/python/python.exe" }
                    else { Join-Path $project 'tmp/ai-media-venv/Scripts/python.exe' }
}
function Start-Decision {
    # A configured chat endpoint needs no copy of the model on this machine.
    if (!$ModelEndpoint) {
        if (!$ModelName) { $ModelName = 'Qwen3-VL-2B-Instruct' }
        if (Test-Path "$portable/llama/llama-server.exe") {
            $cache = Join-Path $portable 'models'
            if (!$LlamaServer) { $LlamaServer = Join-Path $portable 'llama/llama-server.exe' }
        }
        if (!$LlamaServer) { $LlamaServer = Join-Path $cache 'llama-b10887/llama-server.exe' }
        if (!$ModelFile) { $ModelFile = Join-Path $cache 'Qwen3VL-2B-Instruct-Q4_K_M.gguf' }
        if (!$VisionProjector) { $VisionProjector = Join-Path $cache 'mmproj-Qwen3VL-2B-Instruct-Q8_0.gguf' }
        $llamaArgs = @('--host','127.0.0.1','--port',"$LlamaPort",'-m',$ModelFile,'--mmproj',$VisionProjector,'-ngl','99','-c','4096','--alias',$ModelName)
        Start-LocalProcess 'llama' $LlamaServer $llamaArgs "http://127.0.0.1:$LlamaPort/health"
        $ModelEndpoint = "http://127.0.0.1:$LlamaPort/v1/chat/completions"
        $Vision = $true
    }
    if (!$ModelName) { throw 'Set -ModelName to the ID accepted by your chat endpoint.' }
    $gatewayPython = if (Test-Path -LiteralPath $SpeechPython) { $SpeechPython } else { (Get-Command python -ErrorAction Stop).Source }
    $gatewayArgs = @((Join-Path $PSScriptRoot 'decision_service.py'),'--model',$ModelName,'--endpoint',$ModelEndpoint,'--port',"$DecisionPort")
    if ($Vision) { $gatewayArgs += '--images' }
    $gatewayHealth = "http://127.0.0.1:$DecisionPort/v1/health"
    Start-LocalProcess 'decision' $gatewayPython $gatewayArgs $gatewayHealth
    $gateway = Read-Health $gatewayHealth
    if ($gateway.model -ne $ModelName -or [bool]$gateway.images -ne [bool]$Vision) { throw "A different model or image setting is active on port $DecisionPort." }
    if ($gateway.capabilities -notcontains 'replies') { throw "An older decision service is running on port $DecisionPort. Restart it to enable text replies." }
}

function Start-Speech {
    $health = "http://127.0.0.1:$SpeechPort/v1/speech/voices"
    if (Read-Health $health) { Write-Host "speech ready ($health)"; return }
    if (!$SpeechRuntime) {
        $SpeechRuntime = if (Test-Path "$portable/speech/DeliverySpeechRuntime.dll") { "$portable/speech" }
                         else { Join-Path ([IO.Path]::GetPathRoot($project)) 'Udev/Delivery/Saved/DeliverySpeech/runtime' }
    }
    if (!(Test-Path -LiteralPath $SpeechPython)) { throw 'Set -SpeechPython or install scripts/requirements-speech.txt in tmp/ai-media-venv.' }
    $speechArgs = @((Join-Path $PSScriptRoot 'speech_service.py'),'--runtime',$SpeechRuntime,'--port',"$SpeechPort")
    Start-LocalProcess 'speech' $SpeechPython $speechArgs $health
}

$failed = @()
if (!$SkipDecision) {
    try { Start-Decision } catch { $failed += 'decision'; Write-Warning $_.Exception.Message }
}
if (!$SkipSpeech) {
    try { Start-Speech } catch { $failed += 'speech'; Write-Warning $_.Exception.Message }
}
if ($failed.Count -and $RequireAll) { throw ('Could not start: ' + ($failed -join ', ')) }
