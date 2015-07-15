@rem Runs C# tests for given assembly from command line. The Grpc.sln solution needs to be built before running the tests.

setlocal

@rem enter this directory
cd /d %~dp0\..\..\src\csharp

@rem set UUID variable to a random GUID, we will use it to put TestResults.xml to a dedicated directory, so that parallel test runs don't collide
for /F %%i in ('powershell -Command "[guid]::NewGuid().ToString()"') do (set UUID=%%i)

packages\NUnit.Runners.2.6.4\tools\nunit-console-x86.exe -labels "%1/bin/Debug/%1.dll" -work test-results/%UUID% || goto :error

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
