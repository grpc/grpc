@rem Performs nuget restore step for C/C++.

setlocal

@rem enter repo root
cd /d %~dp0\..\..

@rem Location of nuget.exe
set NUGET=C:\nuget\nuget.exe

if exist %NUGET% (
  %NUGET% restore vsprojects/grpc.sln || goto :error
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
