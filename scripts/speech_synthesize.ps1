param([switch]$List)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Speech
$synthesizer = New-Object System.Speech.Synthesis.SpeechSynthesizer
$stream = New-Object IO.MemoryStream
try {
    if ($List) {
        $names = @($synthesizer.GetInstalledVoices() | Where-Object Enabled | ForEach-Object { $_.VoiceInfo.Name })
        [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
        [Console]::Write((ConvertTo-Json -InputObject @{ voices = $names; default_voice = $synthesizer.Voice.Name } -Compress))
        return
    }
    $request = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String([Console]::In.ReadToEnd().Trim())) | ConvertFrom-Json
    if ($request.voice) { $synthesizer.SelectVoice($request.voice) }
    $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
    $synthesizer.SetOutputToAudioStream($stream, $format)
    $synthesizer.Speak($request.text)
    $synthesizer.SetOutputToNull()
    $bytes = $stream.ToArray()
    [Console]::OpenStandardOutput().Write($bytes, 0, $bytes.Length)
} finally {
    $synthesizer.Dispose()
    $stream.Dispose()
}
