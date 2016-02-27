
@rem enter this directory
cd /d %~dp0

@rem extract input artifacts
powershell -Command "Add-Type -Assembly 'System.IO.Compression.FileSystem'; [System.IO.Compression.ZipFile]::ExtractToDirectory('../../../input_artifacts/csharp_nugets.zip', 'TestNugetFeed');"

update_version.sh auto

set NUGET=C:\nuget\nuget.exe
%NUGET% restore || goto :error

@call build_vs2015.bat DistribTest.sln %MSBUILD_EXTRA_ARGS% || goto :error

%DISTRIBTEST_OUTPATH%\DistribTest.exe || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
