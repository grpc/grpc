@rem Runs C# tests for given assembly from command line. The Grpc.sln solution needs to be built before running the tests.

setlocal

if not "%CONFIG%" == "gcov" (
  goto :EOF
)

@rem enter src/csharp directory
cd /d %~dp0\..\..\src\csharp

@rem Generate code coverage report
@rem TODO(jtattermusch): currently the report list is hardcoded
packages\ReportGenerator.2.4.4.0\tools\ReportGenerator.exe -reports:"coverage_csharp_*.xml" -targetdir:"..\..\reports\csharp_coverage" -reporttypes:"Html;TextSummary" || goto :error

@rem Generate the index.html file
echo ^<html^>^<head^>^</head^>^<body^>^<a href='csharp_coverage/index.htm'^>csharp coverage^</a^>^<br/^>^</body^>^</html^> >..\..\reports\index.html

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
