@rem Copyright 2016 gRPC authors.
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem     http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem enter this directory
cd /d %~dp0

@rem extract input artifacts
powershell -Command "Add-Type -Assembly 'System.IO.Compression.FileSystem'; [System.IO.Compression.ZipFile]::ExtractToDirectory('../../../../input_artifacts/csharp_nugets_windows_dotnetcli.zip', 'TestNugetFeed');"

update_version.sh auto

set NUGET=C:\nuget\nuget.exe

@rem TODO(jtattermusch): Get rid of this hack. See #8034
@rem We can't do just "nuget restore" because restoring a .sln solution doesn't work
@rem with nuget 3.X. On the other hand, we need nuget 2.12+ to be able to restore
@rem some of the packages (e.g. System.Interactive.Async), but nuget 2.12
@rem hasn't been officially released.
@rem Please note that "Restore nuget packages" in VS2013 and VS2015 GUI works as usual.

cd DistribTest || goto :error
%NUGET% restore -PackagesDirectory ../packages || goto :error
cd ..

@call build_vs2015.bat DistribTest.sln %MSBUILD_EXTRA_ARGS% || goto :error

%DISTRIBTEST_OUTPATH%\DistribTest.exe || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
