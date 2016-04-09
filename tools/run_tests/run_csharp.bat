@rem Runs C# tests for given assembly from command line. The Grpc.sln solution needs to be built before running the tests.

setlocal

@rem enter src/csharp directory
cd /d %~dp0\..\..\src\csharp

rem nunit3-console fails if nunit.framework.dll is not found.
copy packages\NUnit.3.2.0\lib\net45\nunit.framework.dll packages\NUnit.ConsoleRunner.3.2.0\tools\nunit.framework.dll

if not "%CONFIG%" == "gcov" (
  packages\NUnit.ConsoleRunner.3.2.0\tools\nunit3-console.exe %* || goto :error
) else (
  @rem Run all tests with code coverage

  packages\OpenCover.4.6.519\tools\OpenCover.Console.exe -target:"packages\NUnit.ConsoleRunner.3.2.0\tools\nunit3-console.exe" -targetdir:"." -targetargs:"%*" -filter:"+[Grpc.Core]*"  -register:user -output:coverage_results.xml || goto :error

  packages\ReportGenerator.2.4.4.0\tools\ReportGenerator.exe -reports:"coverage_results.xml" -targetdir:"..\..\reports\csharp_coverage" -reporttypes:"Html;TextSummary" || goto :error

  @rem Generate the index.html file
  echo ^<html^>^<head^>^</head^>^<body^>^<a href='csharp_coverage/index.htm'^>csharp coverage^</a^>^<br/^>^</body^>^</html^> >..\..\reports\index.html
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
