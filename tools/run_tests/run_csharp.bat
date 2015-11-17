@rem Runs C# tests for given assembly from command line. The Grpc.sln solution needs to be built before running the tests.

setlocal

@rem enter src/csharp directory
cd /d %~dp0\..\..\src\csharp

if not "%CONFIG%" == "gcov" (
  @rem Run tests for assembly passed as 1st arg.

  @rem set UUID variable to a random GUID, we will use it to put TestResults.xml to a dedicated directory, so that parallel test runs don't collide
  for /F %%i in ('powershell -Command "[guid]::NewGuid().ToString()"') do (set UUID=%%i)

  packages\NUnit.Runners.2.6.4\tools\nunit-console-x86.exe /domain:None -labels "%1/bin/Debug/%1.dll" -work test-results/%UUID% || goto :error
) else (
  @rem Run all tests with code coverage

  packages\OpenCover.4.6.166\tools\OpenCover.Console.exe -target:"packages\NUnit.Runners.2.6.4\tools\nunit-console-x86.exe" -targetdir:"." -targetargs:"/domain:None -labels Grpc.Core.Tests/bin/Debug/Grpc.Core.Tests.dll Grpc.IntegrationTesting/bin/Debug/Grpc.IntegrationTesting.dll Grpc.Examples.Tests/bin/Debug/Grpc.Examples.Tests.dll Grpc.HealthCheck.Tests/bin/Debug/Grpc.HealthCheck.Tests.dll" -filter:"+[Grpc.Core]*"  -register:user -output:coverage_results.xml || goto :error

  packages\ReportGenerator.2.3.2.0\tools\ReportGenerator.exe -reports:"coverage_results.xml" -targetdir:"..\..\reports\csharp_coverage" -reporttypes:"Html;TextSummary" || goto :error
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
