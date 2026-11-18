# Waits for the next game process, then runs the existing read-only metrics collector on this PC.
[CmdletBinding()]
param(
    [ValidateRange(1,3600)][int]$WaitSeconds=900,
    [ValidateRange(1,86400)][int]$DurationSeconds=1800
)
$ErrorActionPreference='Stop'
try {
    Write-Host 'Aguardando Cyberpunk 2077 neste PC. Pode iniciar o jogo.'
    $collectionDeadline=[DateTime]::UtcNow.AddSeconds($WaitSeconds)
    $gameProcess=$null
    while ([DateTime]::UtcNow -lt $collectionDeadline) {
        $gameProcess=Get-Process -Name Cyberpunk2077 -ErrorAction SilentlyContinue |
            Sort-Object StartTime -Descending | Select-Object -First 1
        if ($gameProcess) { break }
        Start-Sleep -Seconds 2
    }
    if (!$gameProcess) { throw 'O jogo nao foi encontrado em 15 minutos.' }
    Write-Host ('Coletando processo {0}; os dados serao salvos em log/performance-watch do port.' -f $gameProcess.Id)
    & (Join-Path $PSScriptRoot 'collect.ps1') -TargetPid $gameProcess.Id -DurationSeconds $DurationSeconds
    Write-Host 'Coleta encerrada. Os arquivos foram preservados.'
} catch {
    Write-Host ('Falha na coleta: '+$_.Exception.Message)
    exit 1
}
