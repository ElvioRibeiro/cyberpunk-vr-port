param([Parameter(Mandatory)][string]$BuildDirectory)
$ErrorActionPreference='Stop'
$BuildDirectory=[IO.Path]::GetFullPath($BuildDirectory)
cmake -S $PSScriptRoot -B $BuildDirectory -A x64
if ($LASTEXITCODE) { throw 'Configure failed' }
cmake --build $BuildDirectory --config Release
if ($LASTEXITCODE) { throw 'Build failed' }
$collector=Join-Path (Split-Path $PSScriptRoot -Parent) 'collect.ps1'

# Keep fixtures separate from game data and assert recorded behavior, not just exit codes.
function Invoke-Fixture([string]$Mode,[string]$Shell,[int]$Seconds) {
    $hostExe=Join-Path $BuildDirectory 'Release/reader_host.exe'
    $arguments=if ($Mode -eq 'normal') { @() } else { @($Mode) }
    $hostProcess=Start-Process -FilePath $hostExe -ArgumentList $(if ($arguments.Count) {$arguments} else {'normal'}) -WorkingDirectory (Split-Path $hostExe) -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Milliseconds 500
        $output=Join-Path $BuildDirectory ($Mode+'-'+[guid]::NewGuid().ToString('N'))
        & $Shell -NoProfile -ExecutionPolicy Bypass -File $collector -TargetPid $hostProcess.Id -OutputDirectory $output -DurationSeconds $Seconds -IntervalMilliseconds 500 -NoGpu
        if ($LASTEXITCODE) { throw 'Collector failed' }
        $log=Get-ChildItem -LiteralPath $output -Recurse -Filter process.jsonl
        $rows=@(Get-Content -LiteralPath $log.FullName | ConvertFrom-Json)
        $samples=@($rows | Where-Object type -eq sample)
        if ($Mode -eq 'normal') {
            if (@($samples | Where-Object {$_.vrs.status -eq 'ok'}).Count -lt 5) { throw 'Missing valid samples' }
            if (@($rows | Where-Object type -eq vrs_change).Count -lt 2) { throw 'Missing phase changes' }
            if (!($samples | Where-Object {$_.vrs.draws_delta[0] -gt 0 -and $_.vrs.draws_delta[1] -gt 0})) { throw 'Missing deltas' }
        } elseif ($Mode -eq 'stale') {
            if (!($samples | Where-Object {$_.vrs.status -eq 'stale'})) { throw 'Frozen report not detected' }
        } elseif ($Mode -eq 'busy') {
            if ($samples | Where-Object {$_.vrs.status -eq 'ok'}) { throw 'Torn report accepted' }
            if (!($samples | Where-Object {$_.vrs.detail -match 'busy'})) { throw 'Missing busy-report error' }
        }
        Write-Host "PASS $Mode / $Shell"
    } finally { if (!$hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id } }
}
$ps51=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
Invoke-Fixture normal (Get-Command pwsh).Source 6
Invoke-Fixture normal $ps51 6
Invoke-Fixture stale $ps51 5
Invoke-Fixture busy $ps51 2
$missingOutput=Join-Path $BuildDirectory 'missing-module'
& $collector -TargetPid $PID -OutputDirectory $missingOutput -DurationSeconds 2 -NoGpu
$missing=Get-ChildItem -LiteralPath $missingOutput -Recurse -Filter process.jsonl | Select-Object -Last 1
$samples=@(Get-Content -LiteralPath $missing.FullName | ConvertFrom-Json | Where-Object type -eq sample)
if (!($samples | Where-Object {$_.vrs.detail -match 'not loaded'})) { throw 'Missing module not reported' }
Write-Host 'PASS missing module'
