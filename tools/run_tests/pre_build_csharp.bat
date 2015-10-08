@rem Performs nuget restore step for C#.

setlocal

@rem enter repo root
cd /d %~dp0\..\..

@rem Location of nuget.exe
set NUGET=C:\nuget\nuget.exe

if exist %NUGET% (
  %NUGET% restore vsprojects/grpc_csharp_ext.sln || goto :error
  %NUGET% restore src/csharp/Grpc.sln || goto :error
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
