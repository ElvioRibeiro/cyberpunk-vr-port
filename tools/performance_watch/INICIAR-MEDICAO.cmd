@echo off
echo Execute este arquivo como administrador no PC do jogo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-collection.ps1" -Measure
echo.
echo Dados em red4ext\plugins\cyberpunkvrport\log\performance-watch.
pause
