@echo off
set script_dir=%~dp0.
python.exe "%script_dir%\fakeprotoc.py" %*
exit /B %errorlevel%
