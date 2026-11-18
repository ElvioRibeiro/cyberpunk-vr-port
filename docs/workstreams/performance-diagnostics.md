# Performance diagnostics

Base: fork main, 0.2.45. Standalone tools; no rendering/game-setting changes.

Run collect.ps1 or INICIAR-COLETA.cmd on the game PC. Version 2 preserves fractional CPU usage and records script identity. NVIDIA metrics describe the whole adapter.

Version 3 adds a read-only, ASLR-aware reader for the VRS report exports. It respects the producer's odd/even sequence, records per-eye draw deltas, report age/status and automatic requested/effective/stereo transitions. Saved INI settings are sampled separately from proof of GPU application. The wrapper waits up to 30 minutes and records up to one hour; manual event markers share the game PC's UTC.

The reader opens only VM_READ/QUERY_LIMITED_INFORMATION, with module bounds and bounded retries. Missing module/exports, permissions, busy or stale data remain explicit. It requires 64-bit PowerShell; no injection or DLL/config replacement is performed. Logs remain JSONL plus a provenance manifest, readable through the existing network share. FPS and per-frame GPU timing still need another source.

V3 regression: synthetic exported-report DLL/host passed normal cross-process reads and mode transitions in PowerShell 5.1/7; frozen report, busy sequence and missing module cases also passed (five cases). No v3 game-session or remote execution validation is claimed. Run `tools/performance_watch/tests/verify.ps1 -BuildDirectory <absolute-build-directory>` with Visual Studio/CMake and PowerShell 7 available.

Run watch-logs.ps1 on the observing PC with GameRoot and OutputDir. Snapshots are bounded, best-effort and do not monitor remote process counters.

The collector was exercised on Windows PowerShell 5.1/7 and in game sessions; network snapshots were exercised over SMB. No per-frame GPU timing or proof of leak is inferred.

## V4: frame timing and short GPU probes

`INICIAR-MEDICAO.cmd` requires an elevated game-PC shell for PresentMon ETW. The portable console binary is downloaded separately from the official v2.6.0 release and pinned by SHA256. The old start command retains read-only monitoring. A unique ETW session/PID capture records raw QPC and v2 frame/GPU metrics. The collector archives the normal plugin log at stop.

GPU probes use four frames of the existing RenderElements profiler, pipeline statistics and viewport grouping. A separate opt-in handle can write only seven named profiler controls, restores saved settings and never writes render controls. A per-PID mutex prevents duplicate profiling collectors. Warmup, settled-phase delay, periodic sampling, timeout, fresh-report sequence, incomplete-result rejection and probe intervals are explicit. The FPS overlay should be held constant during comparisons.

`analyze-frames.py` aligns raw PresentMon QPC against the collector frequency, separates swapchains/phases, rejects invalid telemetry/timestamp units and excludes marked instrumented intervals plus two seconds. GPU range sums are auxiliary and may overlap. Pixel-shader invocations and range times are normalized by captured frames, not confused with critical-path frame time.

V4 regression: legacy reader cases passed (5), restricted profiler control/restore/interrupted cleanup passed with a synthetic DLL/host on Windows PowerShell 5.1, and CSV phase/clock/exclusion tests passed (3). PresentMon release digest/help checked; non-elevated ETW startup correctly reported access denied and collector recorded exit code 6. Actual elevated game capture, profiler overhead and hardware-counter results remain pending. No DLL changes are needed.
