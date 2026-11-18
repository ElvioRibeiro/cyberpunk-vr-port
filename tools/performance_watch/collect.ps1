# Read-only process/GPU sampling. Run on the game PC; share the output and the port's flight logs afterwards.
[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [string]$GameRoot,
    [int]$TargetPid = 0,
    [ValidateRange(1, 86400)][int]$DurationSeconds = 3600,
    [ValidateRange(250, 60000)][int]$IntervalMilliseconds = 1000,
    [ValidateRange(1, 60)][int]$GpuIntervalSeconds = 5,
    [switch]$NoGpu
)

$ErrorActionPreference = 'Stop'

# Keep missing/unsupported GPU values null rather than reporting a false zero.
function Convert-GpuNumber([string]$Value) {
    $number = 0.0
    if ([double]::TryParse($Value.Trim(), [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) { return $number }
    return $null
}

# Bound the optional nvidia-smi query; the helper never injects into or controls the game.
function Read-GpuSnapshot([string]$Tool) {
    $query = [Diagnostics.ProcessStartInfo]::new()
    $query.FileName = $Tool
    $query.Arguments = '--query-gpu=index,name,utilization.gpu,memory.used,memory.total,temperature.gpu,clocks.gr,clocks.mem,power.draw --format=csv,noheader,nounits'
    $query.UseShellExecute = $false
    $query.CreateNoWindow = $true
    $query.RedirectStandardOutput = $true
    $query.RedirectStandardError = $true
    $queryProcess = [Diagnostics.Process]::new()
    $queryProcess.StartInfo = $query
    try {
        [void]$queryProcess.Start()
        $stdout = $queryProcess.StandardOutput.ReadToEndAsync()
        $stderr = $queryProcess.StandardError.ReadToEndAsync()
        if (-not $queryProcess.WaitForExit(2000)) {
            $queryProcess.Kill()
            [void]$queryProcess.WaitForExit(1000)
            return @{ status = 'timeout'; adapters = @() }
        }
        if ($queryProcess.ExitCode -ne 0) {
            return @{ status = 'failed'; detail = $stderr.Result.Trim(); adapters = @() }
        }
        $fields = 'index', 'name', 'busy', 'used', 'total', 'temperature', 'graphics', 'memory', 'power'
        $adapters = @($stdout.Result -split '\r?\n' | Where-Object { $_.Trim() } |
            ConvertFrom-Csv -Header $fields | ForEach-Object {
                @{
                    index = Convert-GpuNumber $_.index
                    name = $_.name.Trim()
                    utilization_percent = Convert-GpuNumber $_.busy
                    memory_used_mib = Convert-GpuNumber $_.used
                    memory_total_mib = Convert-GpuNumber $_.total
                    temperature_c = Convert-GpuNumber $_.temperature
                    graphics_clock_mhz = Convert-GpuNumber $_.graphics
                    memory_clock_mhz = Convert-GpuNumber $_.memory
                    power_w = Convert-GpuNumber $_.power
                }
            })
        return @{ status = 'ok'; scope = 'whole_gpu_not_game_process'; adapters = $adapters }
    } catch {
        return @{ status = 'failed'; detail = $_.Exception.Message; adapters = @() }
    } finally {
        $queryProcess.Dispose()
    }
}

# Include exact installed-file hashes; neither PR number nor recorder header establishes binary provenance.
function Get-FileIdentity([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $item = Get-Item -LiteralPath $Path
    return @{ path = $item.FullName; bytes = $item.Length; sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
}

$targets = if ($TargetPid) { @(Get-Process -Id $TargetPid) } else { @(Get-Process -Name Cyberpunk2077) }
if ($targets.Count -ne 1) { throw 'Select one running game process with -TargetPid.' }
$target = $targets[0]
$isGame = $target.ProcessName -eq 'Cyberpunk2077'
if ($isGame) {
    $actualRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $target.Path))
    if ($GameRoot -and [IO.Path]::GetFullPath($GameRoot).TrimEnd('\') -ne $actualRoot.TrimEnd('\')) {
        throw 'GameRoot does not match the selected running game. Use the actual installation path.'
    }
    $GameRoot = $actualRoot
}
if (-not $OutputDirectory) {
    $OutputDirectory = if ($isGame) { Join-Path $GameRoot 'red4ext/plugins/cyberpunkvrport/log/performance-watch' }
        else { Join-Path ([IO.Path]::GetTempPath()) 'cpvr-performance-watch' }
}
$sessionName = [DateTimeOffset]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-p' + $target.Id + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6)
$session = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $sessionName
[void][IO.Directory]::CreateDirectory($session)
$encoding = [Text.UTF8Encoding]::new($false)
$logicalProcessors = [Environment]::ProcessorCount
$manifest = @{
    schema = 1; collector_version = 2; collector = Get-FileIdentity $PSCommandPath
    created_utc = [DateTimeOffset]::UtcNow.ToString('o'); host_name = [Environment]::MachineName
    process_id = $target.Id; process_name = $target.ProcessName; process_start_utc = $target.StartTime.ToUniversalTime().ToString('o')
    game_root = $GameRoot; interval_ms = $IntervalMilliseconds; gpu_interval_s = $GpuIntervalSeconds
    logical_processors = $logicalProcessors; qpc_frequency = [Diagnostics.Stopwatch]::Frequency
    mode = 'read_only_process_sampling'; gameplay_validated = $false
    semantics = 'CPU covers the whole process, not one module/thread. GPU metrics cover the whole adapter. No FPS or photon-latency estimate is inferred.'
}
$flightDirectory = $null
if ($GameRoot) {
    $plugin = Join-Path $GameRoot 'red4ext/plugins/cyberpunkvrport'
    $flightDirectory = Join-Path $plugin 'log'
    $manifest.plugin_dll = Get-FileIdentity (Join-Path $plugin 'cyberpunkvrport.dll')
    $manifest.config = Get-FileIdentity (Join-Path $plugin 'config/vrport.ini')
    $manifest.flight_directory = $flightDirectory
}
$gpuTool = if (-not $NoGpu) { Get-Command nvidia-smi -CommandType Application -ErrorAction SilentlyContinue } else { $null }
$manifest.gpu_tool = if ($gpuTool) { $gpuTool.Source } else { $null }
[IO.File]::WriteAllText((Join-Path $session 'manifest.json'), ($manifest | ConvertTo-Json -Depth 8), $encoding)
$writer = [IO.StreamWriter]::new((Join-Path $session 'process.jsonl'), $false, $encoding)
$writer.AutoFlush = $true
Write-Host ('Collecting PID ' + $target.Id + ' into ' + $session)
Write-Host 'Leave the game settings unchanged for the first comparison. Ctrl+C stops collection.'
$startTicks = [Diagnostics.Stopwatch]::GetTimestamp()
$lastTicks = $startTicks
$lastCpuMs = $target.TotalProcessorTime.TotalMilliseconds
$lastGpuSeconds = -$GpuIntervalSeconds
$samples = 0
$reason = 'duration_completed'
try {
    while ($true) {
        $ticks = [Diagnostics.Stopwatch]::GetTimestamp()
        $elapsed = ($ticks - $startTicks) / [double][Diagnostics.Stopwatch]::Frequency
        if ($elapsed -ge $DurationSeconds) { break }
        $target.Refresh()
        if ($target.HasExited) { $reason = 'process_exited'; break }
        $cpuMs = $target.TotalProcessorTime.TotalMilliseconds
        $deltaMs = ($ticks - $lastTicks) * 1000.0 / [Diagnostics.Stopwatch]::Frequency
        # A floating-point zero selects Math.Max(double,double); integer zero rounds fractional core usage.
        $cores = if ($samples -gt 0 -and $deltaMs -gt 0) { [math]::Max(0.0, ($cpuMs - $lastCpuMs) / $deltaMs) } else { $null }
        $row = @{
            type = 'sample'; utc = [DateTimeOffset]::UtcNow.ToString('o')
            qpc_ms = $ticks * 1000.0 / [Diagnostics.Stopwatch]::Frequency; elapsed_s = $elapsed
            process_id = $target.Id; interval_ms = $deltaMs; cpu_total_ms = $cpuMs
            cpu_core_equivalents = $cores
            cpu_machine_percent = if ($null -ne $cores) { $cores * 100.0 / $logicalProcessors } else { $null }
            private_bytes = $target.PrivateMemorySize64; working_set_bytes = $target.WorkingSet64
            virtual_bytes = $target.VirtualMemorySize64; handles = $target.HandleCount; threads = $target.Threads.Count
        }
        $lastTicks = $ticks; $lastCpuMs = $cpuMs
        if ($elapsed - $lastGpuSeconds -ge $GpuIntervalSeconds) {
            $row.gpu_utc = [DateTimeOffset]::UtcNow.ToString('o')
            $row.gpu = if ($gpuTool) { Read-GpuSnapshot $gpuTool.Source }
                else { @{ status = 'unavailable'; adapters = @() } }
            if ($flightDirectory -and (Test-Path -LiteralPath $flightDirectory)) {
                $latest = Get-ChildItem -LiteralPath $flightDirectory -Filter 'framegen-flight-*.jsonl' -File |
                    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
                $row.latest_flight_log = if ($latest) { @{
                    name = $latest.Name; bytes = $latest.Length; last_write_utc = $latest.LastWriteTimeUtc.ToString('o')
                    age_s = ([DateTime]::UtcNow - $latest.LastWriteTimeUtc).TotalSeconds
                } } else { $null }
            }
            $lastGpuSeconds = $elapsed
        }
        $writer.WriteLine(($row | ConvertTo-Json -Depth 8 -Compress))
        ++$samples
        $spentMs = ([Diagnostics.Stopwatch]::GetTimestamp() - $ticks) * 1000.0 / [Diagnostics.Stopwatch]::Frequency
        $remainingMs = [math]::Max(1, $IntervalMilliseconds - $spentMs)
        Start-Sleep -Milliseconds ([int]$remainingMs)
    }
} catch {
    $reason = 'collector_error'
    $writer.WriteLine((@{ type = 'error'; utc = [DateTimeOffset]::UtcNow.ToString('o'); message = $_.Exception.Message } | ConvertTo-Json -Compress))
    throw
} finally {
    if ($reason -eq 'duration_completed' -and
        ([Diagnostics.Stopwatch]::GetTimestamp() - $startTicks) / [double][Diagnostics.Stopwatch]::Frequency -lt $DurationSeconds) {
        $reason = 'interrupted'
    }
    $writer.WriteLine((@{ type = 'stop'; utc = [DateTimeOffset]::UtcNow.ToString('o'); samples = $samples; reason = $reason } | ConvertTo-Json -Compress))
    $writer.Dispose()
    $target.Dispose()
    Write-Host ('Collection saved: ' + $session)
}
