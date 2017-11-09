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
cd /d %~dp0\..\..\..

@rem TODO(jtattermusch): use better install directory
@rem set INSTALL_DIR=%cd%/testinstall
set INSTALL_DIR=C:/testinstall

cd third_party/zlib
mkdir cmake
cd cmake
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ..
cmake --build . --config Release --target install || goto :error
cd ../../..

cd third_party/protobuf/cmake
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% -Dprotobuf_MSVC_STATIC_RUNTIME=OFF -Dprotobuf_BUILD_TESTS=OFF ..
cmake --build . --config Release --target install || goto :error
cd ../../../..

cd third_party/cares/cares
mkdir cmake
cd cmake
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ..
cmake --build . --config Release --target install || goto :error
cd ../../../..

@rem TODO(jtattermusch): what do do with gflags
@rem cd third_party/gflags/cmake
@rem mkdir build
@rem cd build
@rem cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ../..
@rem cmake --build . --config Release --target install || goto :error
@rem cd ../../../..


@rem OpenSSL-Win32 and OpenSSL-Win64 can be downloaded from https://slproweb.com/products/Win32OpenSSL.html
cd cmake
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% -DOPENSSL_ROOT_DIR=C:/OpenSSL-Win32 -DOPENSSL_INCLUDE_DIR=C:/OpenSSL-Win32/include -DZLIB_LIBRARY=%INSTALL_DIR%/lib/zlibstatic.lib -DZLIB_INCLUDE_DIR=%INSTALL_DIR%/include -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DgRPC_PROTOBUF_PROVIDER=package -DgRPC_ZLIB_PROVIDER=package -DgRPC_CARES_PROVIDER=package -DgRPC_SSL_PROVIDER=package -DCMAKE_BUILD_TYPE=Release ../.. || goto :error
cmake --build . --config Release --target install || goto :error
cd ../..

# Build helloworld example using cmake
cd examples/cpp/helloworld
mkdir cmake
cd cmake
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ../.. || goto :error
cmake --build . --config Release || goto :error
cd ../../../../..

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
