@rem Runs C# tests for given assembly from command line. The Grpc.sln solution needs to be built before running the tests.

setlocal

@rem enter this directory
cd /d %~dp0\..\..\src\csharp

packages\NUnit.Runners.2.6.4\tools\nunit-console-x86.exe -labels "%1/bin/Debug/%1.dll" || goto :error

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
