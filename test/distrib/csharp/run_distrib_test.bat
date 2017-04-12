@rem Copyright 2016, Google Inc.
@rem All rights reserved.
@rem
@rem Redistribution and use in source and binary forms, with or without
@rem modification, are permitted provided that the following conditions are
@rem met:
@rem
@rem     * Redistributions of source code must retain the above copyright
@rem notice, this list of conditions and the following disclaimer.
@rem     * Redistributions in binary form must reproduce the above
@rem copyright notice, this list of conditions and the following disclaimer
@rem in the documentation and/or other materials provided with the
@rem distribution.
@rem     * Neither the name of Google Inc. nor the names of its
@rem contributors may be used to endorse or promote products derived from
@rem this software without specific prior written permission.
@rem
@rem THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
@rem "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
@rem LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
@rem A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
@rem OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
@rem SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
@rem LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
@rem DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
@rem THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
@rem (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
@rem OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

@rem enter this directory
cd /d %~dp0

@rem extract input artifacts
powershell -Command "Add-Type -Assembly 'System.IO.Compression.FileSystem'; [System.IO.Compression.ZipFile]::ExtractToDirectory('../../../input_artifacts/csharp_nugets_windows_dotnetcli.zip', 'TestNugetFeed');"

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
