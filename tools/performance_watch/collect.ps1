# Process/GPU sampling; optional ETW capture and bounded internal-profiler requests run on the game PC.
[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [string]$GameRoot,
    [int]$TargetPid = 0,
    [ValidateRange(1, 86400)][int]$DurationSeconds = 3600,
    [ValidateRange(250, 60000)][int]$IntervalMilliseconds = 1000,
    [ValidateRange(1, 60)][int]$GpuIntervalSeconds = 5,
    [switch]$CaptureFrames,
    [switch]$ProfileGpu,
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

# A process-local read-only reader samples the DLL's exported seqlock report.
# Retry module resolution during startup; permission/unsupported-DLL failures stay visible.
$vrsCache = @{ reader=$null; retry_at=0.0; sequence=$null; changed_at=0.0; previous=$null; phase=$null }
if (-not ('CpvrVrsReader' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'VrsReader.cs') }
function Read-VrsSnapshot([int]$ProcessId,[double]$Elapsed) {
    try {
        if (!$vrsCache.reader) {
            if ($Elapsed -lt $vrsCache.retry_at) { return @{status='retry_pending'} }
            $vrsCache.reader = [CpvrVrsReader]::new($ProcessId)
        }
        $report = $vrsCache.reader.Snapshot() | ConvertFrom-Json
        $sequence = $vrsCache.reader.Sequence
        if ($sequence -ne $vrsCache.sequence) { $vrsCache.changed_at=$Elapsed; $vrsCache.sequence=$sequence }
        if ($null -eq $report.requested) { return @{status='initializing'; sequence=$sequence} }
        $delta = @($null,$null)
        if ($vrsCache.previous) {
            for ($eye=0;$eye -lt 2;++$eye) {
                $difference=[double]$report.draws[$eye]-[double]$vrsCache.previous.draws[$eye]
                if ($difference -ge 0) { $delta[$eye]=$difference }
            }
        }
        $phase = '{0}/{1}/{2}' -f $report.requested,$report.applied,$report.stereo
        $phaseChanged = $phase -ne $vrsCache.phase
        $vrsCache.phase=$phase; $vrsCache.previous=$report
        return @{status=$(if ($Elapsed-$vrsCache.changed_at -gt 3) {'stale'} else {'ok'});
            sequence=$sequence; report_age_s=($Elapsed-$vrsCache.changed_at); report=$report;
            draws_delta=$delta; phase_changed=$phaseChanged}
    } catch {
        if ($vrsCache.reader) { $vrsCache.reader.Dispose(); $vrsCache.reader=$null }
        $vrsCache.retry_at=$Elapsed+5
        $vrsCache.previous=$null; $vrsCache.phase=$null
        return @{status='unavailable'; detail=$_.Exception.Message}
    }
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
$profileMutex=$null
if ($ProfileGpu) {
    $profileMutex=[Threading.Mutex]::new($false,('Local\CPVR-GpuProfile-'+$target.Id))
    $owned=$false
    try { $owned=$profileMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned=$true }
    if (!$owned) { $profileMutex.Dispose();throw 'Another GPU profiling collector is already running for this game.' }
}
$sessionName = [DateTimeOffset]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-p' + $target.Id + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6)
$session = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $sessionName
[void][IO.Directory]::CreateDirectory($session)
$encoding = [Text.UTF8Encoding]::new($false)
$logicalProcessors = [Environment]::ProcessorCount
$manifest = @{
    schema = 1; collector_version = 4; collector = Get-FileIdentity $PSCommandPath
    created_utc = [DateTimeOffset]::UtcNow.ToString('o'); host_name = [Environment]::MachineName
    process_id = $target.Id; process_name = $target.ProcessName; process_start_utc = $target.StartTime.ToUniversalTime().ToString('o')
    game_root = $GameRoot; interval_ms = $IntervalMilliseconds; gpu_interval_s = $GpuIntervalSeconds
    logical_processors = $logicalProcessors; qpc_frequency = [Diagnostics.Stopwatch]::Frequency
    vrs_reader = Get-FileIdentity (Join-Path $PSScriptRoot 'VrsReader.cs')
    mode = $(if ($ProfileGpu) {'process_sampling_with_gpu_profile_requests'} else {'read_only_process_sampling_and_vrs_exports'}); gameplay_validated = $false
    semantics = 'CPU covers the whole process, not one module/thread. GPU metrics cover the whole adapter. No FPS or photon-latency estimate is inferred.'
}
$manifest.capture_frames_requested=[bool]$CaptureFrames
$manifest.gpu_profiles_requested=[bool]$ProfileGpu
$manifest.profiler_control_source=Get-FileIdentity (Join-Path $PSScriptRoot 'GpuProfile.ps1')
$manifest.presentmon=Get-FileIdentity (Join-Path $PSScriptRoot 'PresentMon.exe')
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
$presentMonProcess=$null
$presentMonSession=$null
$profileState=@{reader=$null;saved=$null;context=$null;config=@{};next=60.0;inflight=$false;disabled=$false;index=0}
if ($ProfileGpu) { . (Join-Path $PSScriptRoot 'GpuProfile.ps1') }
Write-Host ('Collecting PID ' + $target.Id + ' into ' + $session)
Write-Host 'Leave the game settings unchanged for the first comparison. Ctrl+C stops collection.'
$startTicks = [Diagnostics.Stopwatch]::GetTimestamp()
$lastTicks = $startTicks
$lastCpuMs = $target.TotalProcessorTime.TotalMilliseconds
$lastGpuSeconds = -$GpuIntervalSeconds
$samples = 0
$reason = 'duration_completed'
try {
    if ($CaptureFrames) {
        $presentMonPath=Join-Path $PSScriptRoot 'PresentMon.exe'
        $pmCsv=Join-Path $session 'frames.csv'
        try {
            if (!(Test-Path -LiteralPath $presentMonPath)) { throw 'PresentMon.exe missing beside collect.ps1.' }
            $expected='b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af'
            if ((Get-FileHash -LiteralPath $presentMonPath).Hash.ToLowerInvariant() -ne $expected) { throw 'PresentMon digest mismatch.' }
            $presentMonSession='CPVR-'+$target.Id+'-'+[guid]::NewGuid().ToString('N')
            $pmArgs=@('--process_id',[string]$target.Id,'--output_file',('"'+$pmCsv+'"'),'--qpc_time','--v2_metrics',
                '--no_console_stats','--no_track_input','--session_name',$presentMonSession,
                '--timed',[string]$DurationSeconds,'--terminate_after_timed','--terminate_on_proc_exit')
            $presentMonProcess=Start-Process -FilePath $presentMonPath -ArgumentList $pmArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $session 'presentmon.stdout.log') -RedirectStandardError (Join-Path $session 'presentmon.stderr.log')
            $writer.WriteLine((@{type='presentmon_started';utc=[DateTimeOffset]::UtcNow.ToString('o');process_id=$presentMonProcess.Id;sha256=$expected;version='2.6.0';csv='frames.csv';semantics='Game swapchain presentation, not headset scanout. Convert CPUStartQPC raw ticks using manifest qpc_frequency, then correlate with qpc_ms.'} | ConvertTo-Json -Compress))
        } catch {
            Write-Warning ('Frametimes unavailable: '+$_.Exception.Message)
            $writer.WriteLine((@{type='presentmon_error';utc=[DateTimeOffset]::UtcNow.ToString('o');message=$_.Exception.Message} | ConvertTo-Json -Compress))
        }
    }
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
        if ($flightDirectory -and $elapsed-$lastGpuSeconds -ge $GpuIntervalSeconds) {
            try {
                $row.config_vrs=@{}
                foreach ($line in [IO.File]::ReadAllLines((Join-Path $GameRoot 'red4ext/plugins/cyberpunkvrport/config/vrport.ini'))) {
                    if ($line -match '^\s*(xr_vrs(?:_width|_height|_offset_x|_offset_y)?|xr_engine_stereo|xr_osr|xr_afw|xr_mono_mode)\s*=\s*([^;#]+)') {
                        $row.config_vrs[$Matches[1]]=$Matches[2].Trim()
                    }
                }
            } catch { $row.config_read_error=$_.Exception.Message }
        }
        $row.vrs = Read-VrsSnapshot $target.Id $elapsed
        if ($row.vrs.phase_changed) {
            $writer.WriteLine((@{type='vrs_change';utc=$row.utc;qpc_ms=$row.qpc_ms;
                requested=$row.vrs.report.requested;applied=$row.vrs.report.applied;stereo=$row.vrs.report.stereo} | ConvertTo-Json -Compress))
        }
        if ($ProfileGpu) { Update-GpuProfile $profileState $row $session $writer }
        if ($presentMonProcess -and $presentMonProcess.HasExited) {
            $writer.WriteLine((@{type='presentmon_exit';utc=$row.utc;exit_code=$presentMonProcess.ExitCode} | ConvertTo-Json -Compress))
            Write-Warning ('PresentMon exited with code '+$presentMonProcess.ExitCode+'. Check presentmon.stderr.log.')
            $presentMonProcess.Dispose();$presentMonProcess=$null
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
    if ($ProfileGpu) { try { Close-GpuProfile $profileState } catch { Write-Warning $_.Exception.Message } }
    if ($presentMonProcess) {
        # Only this capture's unique child is stopped; never stop someone else's ETW session.
        if (!$presentMonProcess.HasExited) { [void]$presentMonProcess.WaitForExit(1500) }
        if (!$presentMonProcess.HasExited) {
            try {
                $stopper=Start-Process -FilePath $presentMonPath -ArgumentList @('--session_name',$presentMonSession,'--terminate_existing_session') -WindowStyle Hidden -PassThru
                if (!$stopper.WaitForExit(2000)) { $stopper.Kill() };$stopper.Dispose()
                [void]$presentMonProcess.WaitForExit(2000)
            } catch { Write-Warning $_.Exception.Message }
            if (!$presentMonProcess.HasExited) { $presentMonProcess.Kill() }
        }
        $presentMonProcess.Dispose()
    }
    if ($flightDirectory -and (Test-Path -LiteralPath (Join-Path $flightDirectory 'cyberpunkvrport.log'))) {
        try { Copy-Item -LiteralPath (Join-Path $flightDirectory 'cyberpunkvrport.log') -Destination (Join-Path $session 'cyberpunkvrport-end.log') } catch { Write-Warning $_.Exception.Message }
    }
    if ($vrsCache.reader) { $vrsCache.reader.Dispose() }
    if ($profileMutex) { $profileMutex.ReleaseMutex();$profileMutex.Dispose() }
    $writer.Dispose()
    $target.Dispose()
    Write-Host ('Collection saved: ' + $session)
}
