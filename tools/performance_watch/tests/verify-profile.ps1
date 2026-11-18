param([Parameter(Mandatory)][string]$BuildDirectory)
$ErrorActionPreference='Stop'
$toolRoot=Split-Path $PSScriptRoot -Parent
Add-Type -Path (Join-Path $toolRoot 'VrsReader.cs')
. (Join-Path $toolRoot 'GpuProfile.ps1')
$hostExe=Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) 'Release/reader_host.exe'
$fixture=Start-Process -FilePath $hostExe -WorkingDirectory (Split-Path $hostExe) -WindowStyle Hidden -PassThru
$output=Join-Path $BuildDirectory ('profile-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($output)
$writer=[IO.StreamWriter]::new((Join-Path $output 'events.jsonl'))
$state=@{reader=$null;saved=$null;context=$null;config=@{};next=0.0;inflight=$false;disabled=$false;index=0}
try {
    Start-Sleep -Milliseconds 500
    $reader=[CpvrVrsReader]::new($fixture.Id)
    $denied=$false
    try { $reader.SetProfileControl('CyberpunkVR_SceneGpuGroupMode',1) } catch { $denied=$true }
    $reader.Dispose()
    if (!$denied) { throw 'Read-only reader permitted a write' }
    $row=@{elapsed_s=100.0;process_id=$fixture.Id;utc=[DateTimeOffset]::UtcNow.ToString('o');qpc_ms=100000.0;
        vrs=@{report=@{requested=1;applied=1;stereo=1}};
        config_vrs=@{xr_vrs_width='70';xr_vrs_height='65';xr_vrs_offset_x='0';xr_vrs_offset_y='0'}}
    Update-GpuProfile $state $row $output $writer
    $state.next=0.0
    Update-GpuProfile $state $row $output $writer
    if (!$state.inflight) { throw 'Profile was not requested' }
    Start-Sleep -Milliseconds 500
    $row.elapsed_s=101.0;$row.qpc_ms=101000.0
    Update-GpuProfile $state $row $output $writer
    if ($state.inflight -or $state.disabled) { throw 'Profile did not complete' }
    $profile=Get-Content (Join-Path $output 'gpu-profile-001.json') -Raw | ConvertFrom-Json
    if (!$profile.valid) { throw 'Synthetic fresh profile invalid' }
    if ($state.reader.ProfileControl('CyberpunkVR_SceneGpuPipelineStats') -or $state.reader.ProfileControl('CyberpunkVR_SceneGpuNodeFilter')) { throw 'Diagnostic controls not restored' }
    $denied=$false
    try { $state.reader.SetProfileControl('CyberpunkVR_FixedVrsReportSeq',1) } catch { $denied=$true }
    if (!$denied) { throw 'Profiler permitted a non-profiler write' }
    # A new request must not immediately consume the preceding completed report.
    $state.next=0.0
    Update-GpuProfile $state $row $output $writer
    if (!$state.inflight) { throw 'Second profile not requested' }
    Close-GpuProfile $state
    Write-Host 'PASS read-only protection, restricted writes, fresh report, result, restoration and interrupted cleanup'
} finally { Close-GpuProfile $state;$writer.Dispose();if (!$fixture.HasExited) { Stop-Process -Id $fixture.Id } }
