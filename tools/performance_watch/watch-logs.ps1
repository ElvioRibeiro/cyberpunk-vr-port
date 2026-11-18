param(
    [Parameter(Mandatory)][string]$GameRoot,
    [Parameter(Mandatory)][string]$OutputDir,
    [int]$DurationSeconds = 3600
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$eventsPath = Join-Path $OutputDir 'events.jsonl'
$knownFiles = @{}
$copiedBytes = 0L
$sequence = 0
$startedUtc = [DateTime]::UtcNow

# Records capture provenance and errors locally; never writes to the game share.
function Write-CaptureEvent([hashtable]$EventData) {
    $EventData.utc = [DateTime]::UtcNow.ToString('o')
    $EventData | ConvertTo-Json -Compress -Depth 4 | Add-Content -LiteralPath $eventsPath -Encoding utf8
}

$sources = @(
    (Join-Path $GameRoot 'red4ext/plugins/cyberpunkvrport/log'),
    (Join-Path $GameRoot 'red4ext/logs')
)
Write-CaptureEvent @{event='started'; sources=$sources; duration_seconds=$DurationSeconds; poll_seconds=2}
try {
    while (([DateTime]::UtcNow-$startedUtc).TotalSeconds -lt $DurationSeconds) {
        if (Test-Path -LiteralPath (Join-Path $OutputDir 'stop.request')) { break }
        for ($sourceIndex=0; $sourceIndex -lt $sources.Count; ++$sourceIndex) {
            try {
                $sourceFiles = Get-ChildItem -LiteralPath $sources[$sourceIndex] -File |
                    Where-Object { $_.Extension -in @('.log','.txt','.jsonl') }
                foreach ($sourceFile in $sourceFiles) {
                    $signature = '{0}:{1}' -f $sourceFile.Length,$sourceFile.LastWriteTimeUtc.Ticks
                    if ($knownFiles[$sourceFile.FullName] -eq $signature) { continue }
                    if ($sourceFile.Length -gt 8MB) {
                        Write-CaptureEvent @{event='skipped_large_file';source=$sourceFile.FullName;bytes=$sourceFile.Length}
                        $knownFiles[$sourceFile.FullName]=$signature
                        continue
                    }
                    if ($copiedBytes + $sourceFile.Length -gt 256MB) {
                        Write-CaptureEvent @{event='budget_reached';bytes=$copiedBytes}
                        return
                    }
                    ++$sequence
                    $snapshotName = '{0:D6}-source{1}-{2}' -f $sequence,$sourceIndex,$sourceFile.Name
                    $snapshotPath = Join-Path $OutputDir $snapshotName
                    $inputStream=$null; $outputStream=$null
                    try {
                        $inputStream=[IO.File]::Open($sourceFile.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,
                            ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
                        $outputStream=[IO.File]::Create($snapshotPath)
                        $inputStream.CopyTo($outputStream)
                    } finally {
                        if ($outputStream) { $outputStream.Dispose() }
                        if ($inputStream) { $inputStream.Dispose() }
                    }
                    $snapshotFile=Get-Item -LiteralPath $snapshotPath
                    $copiedBytes += $snapshotFile.Length
                    $knownFiles[$sourceFile.FullName]=$signature
                    Write-CaptureEvent @{event='snapshot';source=$sourceFile.FullName;source_signature=$signature;
                        snapshot=$snapshotName;bytes=$snapshotFile.Length;sha256=(Get-FileHash -LiteralPath $snapshotPath).Hash.ToLowerInvariant()}
                }
            } catch {
                Write-CaptureEvent @{event='read_error';source=$sources[$sourceIndex];message=$_.Exception.Message}
            }
        }
        Start-Sleep -Seconds 2
    }
} finally {
    Write-CaptureEvent @{event='stopped';snapshots=$sequence;copied_bytes=$copiedBytes}
}
