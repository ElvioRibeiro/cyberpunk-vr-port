@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-collection.ps1"
echo.
echo Coleta encerrada; os arquivos estao em log/performance-watch.
pause
