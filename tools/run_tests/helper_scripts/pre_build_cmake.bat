@rem Copyright 2017 gRPC authors.
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

setlocal

cd /d %~dp0\..\..\..

mkdir cmake
cd cmake
mkdir build
cd build

@rem TODO(jtattermusch): Stop hardcoding path to yasm once Jenkins workers can locate yasm correctly
@rem If yasm is not on the path, use hardcoded path instead.
yasm --version || set USE_HARDCODED_YASM_PATH_MAYBE=-DCMAKE_ASM_NASM_COMPILER="C:/Program Files (x86)/yasm/yasm.exe"

cmake -G "Visual Studio 14 2015" -DgRPC_BUILD_TESTS=ON %USE_HARDCODED_YASM_PATH_MAYBE% ../.. || goto :error

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
