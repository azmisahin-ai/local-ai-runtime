#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^https?://127\.0\.0\.1:\d+$')]
    [string]$BaseUrl,

    [string]$ModelId = 'qwen3-asr-0.6b-q8_0',

    [string]$OutputDirectory = (Join-Path $env:TEMP ('lar-turkish-asr-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),

    [string]$UseExistingAudioDirectory = ''
)

$ErrorActionPreference = 'Stop'
$BaseUrl = $BaseUrl.TrimEnd('/')

$testCases = @(
    [pscustomobject]@{ Id = 'sample-01'; Expected = 'Bugün hava güzel.' },
    [pscustomobject]@{ Id = 'sample-02'; Expected = 'Türkçe konuşma tanıma testi yapıyorum.' },
    [pscustomobject]@{ Id = 'sample-03'; Expected = 'Yerel yapay zekâ sesimi yazıya dönüştürüyor.' },
    [pscustomobject]@{ Id = 'sample-04'; Expected = 'Mikrofon kaydı on altı kilohertz örnekleme hızında.' },
    [pscustomobject]@{ Id = 'sample-05'; Expected = 'Kısa cümleleri net ve doğal bir sesle okuyorum.' }
)

function Get-Words {
    param([Parameter(Mandatory = $true)][string]$Text)

    $normalized = $Text.Normalize([System.Text.NormalizationForm]::FormKC)
    $culture = [System.Globalization.CultureInfo]::GetCultureInfo('tr-TR')
    $normalized = $culture.TextInfo.ToLower($normalized)
    # Circumflex spellings (â/î/û) are orthographic variants for WER scoring.
    $normalized = $normalized.Replace('â', 'a').Replace('î', 'i').Replace('û', 'u')
    $wordMatches = [regex]::Matches($normalized, '[\p{L}\p{Nd}]+')
    $words = New-Object 'System.Collections.Generic.List[string]'
    foreach ($wordMatch in $wordMatches) { [void]$words.Add($wordMatch.Value) }
    $wordArray = $words.ToArray()
    return ,$wordArray
}

function Get-WordEditDistance {
    param(
        [Parameter(Mandatory = $true)][string[]]$Reference,
        [Parameter(Mandatory = $true)][string[]]$Hypothesis
    )

    $previous = New-Object 'int[]' ($Hypothesis.Length + 1)
    for ($j = 0; $j -le $Hypothesis.Length; $j++) { $previous[$j] = $j }

    for ($i = 1; $i -le $Reference.Length; $i++) {
        $current = New-Object 'int[]' ($Hypothesis.Length + 1)
        $current[0] = $i
        for ($j = 1; $j -le $Hypothesis.Length; $j++) {
            $substitutionCost = 0
            if ($Reference[$i - 1] -cne $Hypothesis[$j - 1]) { $substitutionCost = 1 }
            $deletion = $previous[$j] + 1
            $insertion = $current[$j - 1] + 1
            $substitution = $previous[$j - 1] + $substitutionCost
            $current[$j] = [Math]::Min($deletion, [Math]::Min($insertion, $substitution))
        }
        $previous = $current
    }
    return $previous[$Hypothesis.Length]
}

# Check the runtime and active audio.cpp engine before recording or uploading anything.
$engines = Invoke-RestMethod -Uri ($BaseUrl + '/v1/engines') -Method Get -TimeoutSec 10
$engine = @($engines.data | Where-Object { $_.id -eq 'audio-cpp' })[0]
if ($null -eq $engine -or !$engine.ready -or $engine.status -ne 'running') {
    throw 'audio-cpp must be started and Ready before running the ASR acceptance test.'
}

$upstreamBaseUrl = ([string]$engine.upstream_base_url).TrimEnd('/')
if ([string]::IsNullOrWhiteSpace($upstreamBaseUrl)) { throw 'audio.cpp upstream URL is missing.' }
$health = Invoke-RestMethod -Uri ($upstreamBaseUrl + '/health') -Method Get -TimeoutSec 10
if ($health.status -ne 'ok') { throw 'audio.cpp health check did not return status=ok.' }
$backend = [string]$health.backend
if ($backend -notin @('cpu', 'cuda')) { throw ('Unexpected audio.cpp backend: ' + $backend) }
$models = Invoke-RestMethod -Uri ($upstreamBaseUrl + '/v1/models') -Method Get -TimeoutSec 10
$registered = @($models.data | Where-Object { $_.id -eq $ModelId })[0]
if ($null -eq $registered -or !$registered.configured) {
    throw ('Model is not configured in audio.cpp: ' + $ModelId)
}

if ([string]::IsNullOrWhiteSpace($UseExistingAudioDirectory)) {
    $audioDirectory = Join-Path $OutputDirectory 'audio'
    New-Item -ItemType Directory -Path $audioDirectory -Force | Out-Null
} else {
    $audioDirectory = (Resolve-Path -LiteralPath $UseExistingAudioDirectory).Path
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

if (-not ('LARAsrAcceptanceCapture' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public static class LARAsrAcceptanceCapture
{
    [StructLayout(LayoutKind.Sequential)]
    private struct WaveFormat
    {
        public ushort formatTag;
        public ushort channels;
        public uint samplesPerSecond;
        public uint averageBytesPerSecond;
        public ushort blockAlign;
        public ushort bitsPerSample;
        public ushort extraSize;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct WaveHeader
    {
        public IntPtr data;
        public uint bufferLength;
        public uint bytesRecorded;
        public IntPtr user;
        public uint flags;
        public uint loops;
        public IntPtr next;
        public IntPtr reserved;
    }

    [DllImport("winmm.dll")]
    private static extern uint waveInOpen(out IntPtr handle, int device, ref WaveFormat format,
        IntPtr callback, IntPtr instance, uint flags);
    [DllImport("winmm.dll")]
    private static extern uint waveInPrepareHeader(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")]
    private static extern uint waveInAddBuffer(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")]
    private static extern uint waveInStart(IntPtr handle);
    [DllImport("winmm.dll")]
    private static extern uint waveInStop(IntPtr handle);
    [DllImport("winmm.dll")]
    private static extern uint waveInReset(IntPtr handle);
    [DllImport("winmm.dll")]
    private static extern uint waveInUnprepareHeader(IntPtr handle, IntPtr header, uint size);
    [DllImport("winmm.dll")]
    private static extern uint waveInClose(IntPtr handle);

    private static void Check(uint result, string operation)
    {
        if (result != 0) throw new InvalidOperationException(operation + " failed. WinMM error: " + result);
    }

    public static void Record(string path, int seconds)
    {
        const int rate = 16000;
        const int bytesPerSample = 2;
        int capacity = rate * bytesPerSample * seconds;
        IntPtr handle = IntPtr.Zero;
        IntPtr data = IntPtr.Zero;
        IntPtr headerPtr = IntPtr.Zero;
        bool opened = false;
        bool prepared = false;
        uint headerSize = (uint)Marshal.SizeOf(typeof(WaveHeader));

        try
        {
            WaveFormat format = new WaveFormat();
            format.formatTag = 1;
            format.channels = 1;
            format.samplesPerSecond = rate;
            format.averageBytesPerSecond = rate * bytesPerSample;
            format.blockAlign = bytesPerSample;
            format.bitsPerSample = 16;
            format.extraSize = 0;

            Check(waveInOpen(out handle, -1, ref format, IntPtr.Zero, IntPtr.Zero, 0), "waveInOpen");
            opened = true;
            data = Marshal.AllocHGlobal(capacity);
            headerPtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(WaveHeader)));
            Marshal.Copy(new byte[capacity], 0, data, capacity);

            WaveHeader header = new WaveHeader();
            header.data = data;
            header.bufferLength = (uint)capacity;
            Marshal.StructureToPtr(header, headerPtr, false);
            Check(waveInPrepareHeader(handle, headerPtr, headerSize), "waveInPrepareHeader");
            prepared = true;
            Check(waveInAddBuffer(handle, headerPtr, headerSize), "waveInAddBuffer");
            Check(waveInStart(handle), "waveInStart");
            Thread.Sleep(seconds * 1000);
            Check(waveInStop(handle), "waveInStop");

            WaveHeader completed = (WaveHeader)Marshal.PtrToStructure(headerPtr, typeof(WaveHeader));
            int length = (int)completed.bytesRecorded;
            if (length < 6400) throw new InvalidOperationException("Captured audio is too short or the microphone is unavailable.");

            byte[] audio = new byte[length];
            Marshal.Copy(data, audio, 0, length);
            using (FileStream fs = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (BinaryWriter writer = new BinaryWriter(fs))
            {
                writer.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));
                writer.Write((uint)(audio.Length + 36));
                writer.Write(System.Text.Encoding.ASCII.GetBytes("WAVE"));
                writer.Write(System.Text.Encoding.ASCII.GetBytes("fmt "));
                writer.Write((uint)16);
                writer.Write((ushort)1);
                writer.Write((ushort)1);
                writer.Write((uint)rate);
                writer.Write((uint)(rate * bytesPerSample));
                writer.Write((ushort)bytesPerSample);
                writer.Write((ushort)16);
                writer.Write(System.Text.Encoding.ASCII.GetBytes("data"));
                writer.Write((uint)audio.Length);
                writer.Write(audio);
            }
        }
        finally
        {
            if (opened) waveInReset(handle);
            if (prepared) waveInUnprepareHeader(handle, headerPtr, headerSize);
            if (opened) waveInClose(handle);
            if (headerPtr != IntPtr.Zero) Marshal.FreeHGlobal(headerPtr);
            if (data != IntPtr.Zero) Marshal.FreeHGlobal(data);
        }
    }
}
'@
}

$results = New-Object 'System.Collections.Generic.List[object]'
$totalReferenceWords = 0
$totalErrors = 0
$recordSeconds = 8

foreach ($case in $testCases) {
    $wavPath = Join-Path $audioDirectory ($case.Id + '.wav')
    if ([string]::IsNullOrWhiteSpace($UseExistingAudioDirectory)) {
        Write-Host ''
        Write-Host ('Test cümlesi: ' + $case.Expected) -ForegroundColor Cyan
        [void](Read-Host 'Hazır olduğunda Enter tuşuna bas; kayıt hemen başlayacak')
        Write-Host ('Kayıt başladı — ' + $recordSeconds + ' saniye konuş.') -ForegroundColor Yellow
        [LARAsrAcceptanceCapture]::Record($wavPath, $recordSeconds)
    }

    if (!(Test-Path -LiteralPath $wavPath)) {
        throw ('Required WAV is missing: ' + $case.Id + '.wav')
    }

    $wavItem = Get-Item -LiteralPath $wavPath
    if ($wavItem.Length -lt 44 -or $wavItem.Length -gt 24MB) {
        throw ('WAV size is outside allowed bounds: ' + $case.Id)
    }
    $wavStream = [System.IO.File]::OpenRead($wavPath)
    try {
        $wavHeader = New-Object byte[] 12
        $headerRead = $wavStream.Read($wavHeader, 0, 12)
        if ($headerRead -ne 12 -or
            [System.Text.Encoding]::ASCII.GetString($wavHeader, 0, 4) -ne 'RIFF' -or
            [System.Text.Encoding]::ASCII.GetString($wavHeader, 8, 4) -ne 'WAVE') {
            throw ('Invalid RIFF/WAVE fixture: ' + $case.Id)
        }
    }
    finally { $wavStream.Dispose() }

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $result = Invoke-RestMethod -Uri ($BaseUrl + '/v1/tasks/asr/transcribe') `
        -Method Post -InFile $wavPath -ContentType 'audio/wav' `
        -Headers @{ 'X-LAR-Model' = $ModelId; 'X-LAR-Language' = 'tr' } `
        -TimeoutSec 900
    $timer.Stop()

    if ($result.task -ne 'asr.transcribe' -or [string]::IsNullOrWhiteSpace([string]$result.text)) {
        throw ('ASR returned an empty or unexpected result for ' + $case.Id)
    }
    if ([string]$result.language -ne 'tr') {
        throw ('Normalized ASR response did not preserve requested language=tr for ' + $case.Id)
    }

    $referenceWords = Get-Words -Text $case.Expected
    $hypothesisWords = Get-Words -Text ([string]$result.text)
    $errors = Get-WordEditDistance -Reference $referenceWords -Hypothesis $hypothesisWords
    $sampleWer = if ($referenceWords.Length -gt 0) { [double]$errors / $referenceWords.Length } else { 0.0 }
    $totalReferenceWords += $referenceWords.Length
    $totalErrors += $errors

    $results.Add([pscustomobject]@{
        sample_id = $case.Id
        reference = $case.Expected
        hypothesis = [string]$result.text
        requested_language = 'tr'
        response_language = [string]$result.language
        reference_words = $referenceWords.Length
        edit_errors = $errors
        wer = [Math]::Round($sampleWer, 4)
        elapsed_seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
        audio_bytes = $wavItem.Length
    })

    Write-Host ('{0}: WER {1:P1} | {2:N2} s' -f $case.Id, $sampleWer, $timer.Elapsed.TotalSeconds)
    Write-Host ('  Beklenen: ' + $case.Expected)
    Write-Host ('  Tanınan : ' + [string]$result.text)
}

$overallWer = if ($totalReferenceWords -gt 0) { [double]$totalErrors / $totalReferenceWords } else { 0.0 }
$report = [pscustomobject]@{
    schema = 'turkish-asr-acceptance-v1'
    engine = 'audio.cpp'
    audio_cpp_version = [string]$health.version
    backend = $backend
    model_id = $ModelId
    requested_language = 'tr'
    sample_count = $results.Count
    reference_word_count = $totalReferenceWords
    edit_errors = $totalErrors
    overall_wer = [Math]::Round($overallWer, 4)
    samples = @($results.ToArray())
}

$reportPath = Join-Path $OutputDirectory ('asr-acceptance-' + $backend + '.json')
$json = $report | ConvertTo-Json -Depth 8
$utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
[System.IO.File]::WriteAllText($reportPath, $json, $utf8NoBom)

Write-Host ''
Write-Host ('Backend: ' + $backend)
Write-Host ('Overall WER: {0:P2} ({1}/{2} word edits)' -f $overallWer, $totalErrors, $totalReferenceWords)
Write-Host ('Report: ' + $reportPath) -ForegroundColor Green
Write-Host 'Recordings and reports are stored locally; this script calls only the supplied 127.0.0.1 runtime URL.'
