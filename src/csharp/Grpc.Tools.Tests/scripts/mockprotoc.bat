@echo off
set script_dir= %~dp0.
powershell.exe -NoLogo -ExecutionPolicy Unrestricted -File "%script_dir%\mockprotoc.ps1" %*
exit /B %errorlevel%
