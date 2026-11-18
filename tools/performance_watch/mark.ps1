param([string]$Label)
$ErrorActionPreference='Stop'
if (!$Label) { $Label=Read-Host 'Descreva o evento (ex.: inicio baseline, queda, cintilacao)' }
if ([string]::IsNullOrWhiteSpace($Label)) { exit 0 }
# Timestamp on the game PC is shared with the collector's UTC; never compare QPC across PCs.
$gameRoot=Split-Path -Parent $PSScriptRoot
$output=Join-Path $gameRoot 'red4ext/plugins/cyberpunkvrport/log/performance-watch'
[void][IO.Directory]::CreateDirectory($output)
$row=@{type='user_marker';utc=[DateTimeOffset]::UtcNow.ToString('o');label=$Label}
$line=($row | ConvertTo-Json -Compress)+[Environment]::NewLine
[IO.File]::AppendAllText((Join-Path $output 'markers.jsonl'),$line,[Text.UTF8Encoding]::new($false))
Write-Host 'Evento registrado.'
