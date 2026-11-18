# Waits for the game; -Measure adds ETW capture and explicit profiler-control writes.
[CmdletBinding()]
param(
    [ValidateRange(1,3600)][int]$WaitSeconds=1800,
    [ValidateRange(1,86400)][int]$DurationSeconds=3600,
    [switch]$Measure
)
$ErrorActionPreference='Stop'
try {
    if ($Measure) {
        $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
        $principal=[Security.Principal.WindowsPrincipal]::new($identity)
        if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Use Executar como administrador no INICIAR-MEDICAO.cmd para habilitar ETW/PresentMon.' }
    }
    Write-Host 'Aguardando Cyberpunk 2077 neste PC. Pode iniciar o jogo.'
    $collectionDeadline=[DateTime]::UtcNow.AddSeconds($WaitSeconds)
    $gameProcess=$null
    while ([DateTime]::UtcNow -lt $collectionDeadline) {
        $gameProcess=Get-Process -Name Cyberpunk2077 -ErrorAction SilentlyContinue |
            Sort-Object StartTime -Descending | Select-Object -First 1
        if ($gameProcess) { break }
        Start-Sleep -Seconds 2
    }
    if (!$gameProcess) { throw 'O jogo nao foi encontrado no intervalo configurado.' }
    Write-Host ('Coletando processo {0}; os dados serao salvos em log/performance-watch do port.' -f $gameProcess.Id)
    & (Join-Path $PSScriptRoot 'collect.ps1') -TargetPid $gameProcess.Id -DurationSeconds $DurationSeconds -CaptureFrames:$Measure -ProfileGpu:$Measure
    Write-Host 'Coleta encerrada. Os arquivos foram preservados.'
} catch {
    Write-Host ('Falha na coleta: '+$_.Exception.Message)
    exit 1
}
