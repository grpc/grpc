@rem Copyright 2016, gRPC authors
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem   http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem Performs nuget restore step for C#.

setlocal

set ARCHITECTURE=%1

@rem enter repo root
cd /d %~dp0\..\..\..

mkdir cmake
cd cmake
mkdir build
cd build
mkdir %ARCHITECTURE%
cd %ARCHITECTURE%
@rem TODO(jtattermusch): Stop hardcoding path to yasm once Jenkins workers can locate yasm correctly
cmake -G "Visual Studio 14 2015" -A %ARCHITECTURE% -DgRPC_BUILD_TESTS=OFF -DCMAKE_ASM_NASM_COMPILER="C:/Program Files (x86)/yasm/yasm.exe" ../../.. || goto :error
cd ..\..\..

@rem Location of nuget.exe
set NUGET=C:\nuget\nuget.exe

if exist %NUGET% (
  @rem TODO(jtattermusch): Get rid of this hack. See #8034
  @rem Restore Grpc packages by packages since Nuget client 3.4.4 doesnt support restore
  @rem by solution
  @rem Moving into each directory to let the restores work based on per-project packages.config files

  cd src/csharp

  cd Grpc.Auth || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.Core || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.Core.Tests || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.Examples.MathClient || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.Examples.MathServer || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.Examples || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.HealthCheck.Tests || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.HealthCheck || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.IntegrationTesting.Client || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.IntegrationTesting.QpsWorker || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.IntegrationTesting.StressClient || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd Grpc.IntegrationTesting || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error

  cd /d %~dp0\..\.. || goto :error
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
