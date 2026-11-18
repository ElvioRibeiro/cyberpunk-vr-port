# Performance diagnostics

Base: fork main, 0.2.45. Standalone tools; no rendering/game-setting changes.

Run collect.ps1 or INICIAR-COLETA.cmd on the game PC. Version 2 preserves fractional CPU usage and records script identity. NVIDIA metrics describe the whole adapter.

Run watch-logs.ps1 on the observing PC with GameRoot and OutputDir. Snapshots are bounded, best-effort and do not monitor remote process counters.

The collector was exercised on Windows PowerShell 5.1/7 and in game sessions; network snapshots were exercised over SMB. No per-frame GPU timing or proof of leak is inferred.
