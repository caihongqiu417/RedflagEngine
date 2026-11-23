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
    [string]$QwenSpeechProject = $env:REDFLAG_QWEN_TTS_PROJECT,
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
$settingsFile = Join-Path $env:LOCALAPPDATA 'Redflag/services.json'
$settings = if (Test-Path $settingsFile) { Get-Content -LiteralPath $settingsFile -Encoding UTF8 -Raw | ConvertFrom-Json }
if (!$ModelEndpoint -and $settings.UseExternalModel) {
    $ModelEndpoint = $settings.ModelEndpoint
    if (!$ModelName) { $ModelName = $settings.ModelName }
    if (!$PSBoundParameters.ContainsKey('Vision')) { $Vision = [bool]$settings.Vision }
}

function Read-Health([string]$url) {
    try { Invoke-RestMethod -Uri $url -TimeoutSec 2 -ErrorAction Stop } catch { $null }
}

function Find-ServicePort([int]$port, [string]$route, [string]$name, [scriptblock]$compatible) {
    $listeners = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners().Port
    # A lower port can become free after another program closes. Prefer this project's resident model.
    $previous = Get-ChildItem -LiteralPath $logs -Filter "$name-*.out" | Sort-Object LastWriteTime -Descending
    foreach ($log in $previous) {
        if ($log.BaseName -match "^$name-(\d+)$") {
            $candidate = [int]$Matches[1]
            if ($candidate -lt $port -or $listeners -notcontains $candidate) { continue }
            $status = Read-Health "http://127.0.0.1:$candidate/$route"
            if ($status -and (& $compatible $status)) { return $candidate }
        }
    }
    while ($listeners -contains $port) {
        $status = Read-Health "http://127.0.0.1:$port/$route"
        if ($status -and (& $compatible $status)) { break }
        $port++
    }
    return $port
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
    $digest = [Security.Cryptography.SHA256]::Create()
    try { $endpointId = [BitConverter]::ToString($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($ModelEndpoint))).Replace('-','').ToLowerInvariant() }
    finally { $digest.Dispose() }
    $DecisionPort = Find-ServicePort $DecisionPort 'v1/health' 'decision' {
        param($service)
        $service.endpoint_id -eq $endpointId -and $service.history -and $service.model -eq $ModelName -and [bool]$service.images -eq [bool]$Vision
    }
    $gatewayPython = if (Test-Path -LiteralPath $SpeechPython) { $SpeechPython } else { (Get-Command python -ErrorAction Stop).Source }
    $gatewayArgs = @((Join-Path $PSScriptRoot 'decision_service.py'),'--model',$ModelName,'--endpoint',$ModelEndpoint,'--port',"$DecisionPort")
    if ($Vision) { $gatewayArgs += '--images' }
    $gatewayHealth = "http://127.0.0.1:$DecisionPort/v1/health"
    Start-LocalProcess "decision-$DecisionPort" $gatewayPython $gatewayArgs $gatewayHealth
    $gateway = Read-Health $gatewayHealth
    if ($gateway.model -ne $ModelName -or [bool]$gateway.images -ne [bool]$Vision) { throw "A different model or image setting is active on port $DecisionPort." }
    if ($gateway.capabilities -notcontains 'replies') { throw "An older decision service is running on port $DecisionPort. Restart it to enable text replies." }
    $env:REDFLAG_DECISION_PORT = "$DecisionPort"
}

function Start-Speech {
    $preference = Join-Path $env:LOCALAPPDATA 'Redflag/voice.json'
    if (!$QwenSpeechProject -and $settings -and $settings.PSObject.Properties['QwenSpeechProject']) {
        $QwenSpeechProject = $settings.QwenSpeechProject
    }
    elseif (!$QwenSpeechProject -and (Test-Path $preference)) {
        $localProject = (Get-Content -LiteralPath $preference -Encoding UTF8 -Raw | ConvertFrom-Json).QwenSpeechProject
        if ($localProject -and (Test-Path "$localProject/Tools/DeliveryAI/neural_tts.py")) { $QwenSpeechProject = $localProject }
    }
    if ($QwenSpeechProject) { $QwenSpeechProject = (Resolve-Path -LiteralPath $QwenSpeechProject).Path }
    $assetRoot = if (Test-Path "$project/RedflagForge/assets") { "$project/RedflagForge" } else { $project }
    $assetRoot = (Resolve-Path -LiteralPath $assetRoot).Path

    # Other games and older releases may already use the usual port. Keep their service alive.
    $SpeechPort = Find-ServicePort $SpeechPort 'v1/speech/health' 'speech' {
        param($service)
        $service.bank_speech -eq 4 -and $service.asset_root -eq $assetRoot -and
            [string]$service.qwen_project -eq [string]$QwenSpeechProject
    }
    if (!$SpeechRuntime) {
        $SpeechRuntime = if (Test-Path "$portable/speech/DeliverySpeechRuntime.dll") { "$portable/speech" }
                         else { Join-Path ([IO.Path]::GetPathRoot($project)) 'Udev/Delivery/Saved/DeliverySpeech/runtime' }
    }
    if (!(Test-Path -LiteralPath $SpeechPython)) { throw 'Set -SpeechPython or install scripts/requirements-speech.txt in tmp/ai-media-venv.' }
    $speechArgs = @((Join-Path $PSScriptRoot 'speech_service.py'),'--runtime',$SpeechRuntime,'--port',"$SpeechPort")
    if ($QwenSpeechProject) { $speechArgs += @('--qwen-project',$QwenSpeechProject) }
    Start-LocalProcess "speech-$SpeechPort" $SpeechPython $speechArgs "http://127.0.0.1:$SpeechPort/v1/speech/health"
    $env:REDFLAG_SPEECH_PORT = "$SpeechPort"
}

$failed = @()
if (!$SkipDecision) {
    try { Start-Decision } catch { $failed += 'decision'; Write-Warning $_.Exception.Message }
}
if (!$SkipSpeech) {
    try { Start-Speech } catch { $failed += 'speech'; Write-Warning $_.Exception.Message }
}
if ($failed.Count -and $RequireAll) { throw ('Could not start: ' + ($failed -join ', ')) }
