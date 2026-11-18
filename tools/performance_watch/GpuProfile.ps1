# Four-frame probes use the existing exported profiler controls, never render settings.
function Restore-ProfileControls($State) {
    if ($State.reader -and $State.saved) {
        foreach ($name in $State.saved.Keys) { $State.reader.SetProfileControl($name,[uint32]$State.saved[$name]) }
        $State.saved=$null
    }
}

# Restore diagnostic settings even when collection is interrupted; do not terminate an active GPU workload.
function Close-GpuProfile($State) {
    if (!$State.reader) { return }
    try {
        if ($State.inflight -and $State.reader.ProfileControl('CyberpunkVR_SceneGpuFrameRequest') -eq 4) {
            $State.reader.SetProfileControl('CyberpunkVR_SceneGpuFrameRequest',0)
        }
        Restore-ProfileControls $State
    } finally { $State.reader.Dispose();$State.reader=$null }
}

# Nonblocking state machine: wait for steady settings, request once, poll and save after GPU completion.
function Update-GpuProfile($State,$Row,[string]$Session,$Writer) {
    $elapsed=[double]$Row.elapsed_s
    if ($Row.config_vrs) { $State.config=$Row.config_vrs }
    if ($Row.vrs.status -and $Row.vrs.status -ne 'ok' -and !$State.inflight) { return }
    $report=$Row.vrs.report
    if (!$report) { return }
    $region=@('xr_vrs_width','xr_vrs_height','xr_vrs_offset_x','xr_vrs_offset_y') | ForEach-Object { $State.config[$_] }
    $context=(@($report.requested,$report.applied,$report.stereo)+$region) -join '/'
    if ($context -ne $State.context) { $State.context=$context;$State.next=[math]::Max(60.0,$elapsed+20) }
    if ($State.disabled) { return }
    try {
        if ($State.inflight) {
            $profile=$State.reader.ProfileSnapshot() | ConvertFrom-Json
            if ($State.reader.ProfileSequence -ne $State.beforeSeq -and !$profile.active -and $profile.mode -eq 'frames' -and $profile.completed) {
                $hasStats=@($profile.groups | Where-Object { $_.node -eq 3399076 -and $_.ps.Count -eq 11 }).Count -gt 0
                $valid= $hasStats -and !$profile.error -and !$profile.exhausted -and !$profile.discarded -and !$profile.pending -and !$profile.recordings -and $profile.frames -eq 4 -and $context -eq $State.captureContext
                $filename='gpu-profile-'+$State.index.ToString('D3')+'.json'
                $envelope=@{utc=$Row.utc;start_qpc_ms=$State.startQpc;end_qpc_ms=$Row.qpc_ms;
                    exclude_until_qpc_ms=($Row.qpc_ms+2000);context=$State.captureContext;valid=$valid;report=$profile;
                    semantics='Range sums can overlap between queues; not total critical-path GPU frame time.'}
                [IO.File]::WriteAllText((Join-Path $Session $filename),($envelope | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
                $Writer.WriteLine((@{type='gpu_profile_end';utc=$Row.utc;qpc_ms=$Row.qpc_ms;file=$filename;valid=$valid;exclude_until_qpc_ms=($Row.qpc_ms+2000)} | ConvertTo-Json -Compress))
                Restore-ProfileControls $State
                $State.inflight=$false;$State.next=$elapsed+45
            } elseif ($elapsed-$State.started -gt 15) { throw 'GPU profile timed out; profiling disabled for this session.' }
            return
        }
        if ($elapsed -lt $State.next -or ($report.requested -ne 0 -and $report.applied -eq 0)) { return }
        if (!$State.reader) { $State.reader=[CpvrVrsReader]::new([int]$Row.process_id,$true) }
        $profile=$State.reader.ProfileSnapshot() | ConvertFrom-Json
        if ($profile.active -or $State.reader.ProfileControl('CyberpunkVR_SceneGpuFrameRequest') -or $State.reader.ProfileControl('CyberpunkVR_SceneGpuRequest')) {
            $State.next=$elapsed+5;return
        }
        $State.beforeSeq=$State.reader.ProfileSequence
        $controls=@{CyberpunkVR_SceneGpuGroupMode=1;CyberpunkVR_SceneGpuNodeFilter=3399076;
            CyberpunkVR_SceneGpuViewportDetail=1;CyberpunkVR_SceneGpuPipelineStats=1;CyberpunkVR_SceneGpuDrawOcclusion=0}
        # 3399076 = 0x33DDA4 (RenderElements in this game-2.5 base).
        $State.saved=@{}
        foreach ($name in $controls.Keys) { $State.saved[$name]=$State.reader.ProfileControl($name) }
        foreach ($name in $controls.Keys) { $State.reader.SetProfileControl($name,[uint32]$controls[$name]) }
        ++$State.index;$State.started=$elapsed;$State.startQpc=$Row.qpc_ms;$State.captureContext=$context
        $State.inflight=$true
        $Writer.WriteLine((@{type='gpu_profile_start';utc=$Row.utc;qpc_ms=$Row.qpc_ms;context=$context;frames=4} | ConvertTo-Json -Compress))
        $State.reader.SetProfileControl('CyberpunkVR_SceneGpuFrameRequest',4)
    } catch {
        $Writer.WriteLine((@{type='gpu_profile_error';utc=$Row.utc;qpc_ms=$Row.qpc_ms;message=$_.Exception.Message;exclude_until_qpc_ms=($Row.qpc_ms+2000)} | ConvertTo-Json -Compress))
        try { Close-GpuProfile $State } catch { }
        $State.disabled=$true
    }
}
