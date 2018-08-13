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

@rem TODO(jtattermusch): Kokoro has pre-installed protoc.exe in C:\Program Files\ProtoC and that directory
@rem is on PATH. To avoid picking up the older version protoc.exe, we change the path to something non-existent.
set PATH=%PATH:ProtoC=DontPickupProtoC%

@rem Install into ./testinstall, but use absolute path and foward slashes
set INSTALL_DIR=%cd:\=/%/testinstall

@rem Download OpenSSL-Win32 originally installed from https://slproweb.com/products/Win32OpenSSL.html
powershell -Command "(New-Object Net.WebClient).DownloadFile('https://storage.googleapis.com/grpc-testing.appspot.com/OpenSSL-Win32-1_1_0g.zip', 'OpenSSL-Win32.zip')"
powershell -Command "Add-Type -Assembly 'System.IO.Compression.FileSystem'; [System.IO.Compression.ZipFile]::ExtractToDirectory('OpenSSL-Win32.zip', '.');"

@rem set absolute path to OpenSSL with forward slashes
set OPENSSL_DIR=%cd:\=/%/OpenSSL-Win32

cd third_party/zlib
mkdir cmake
cd cmake
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ..
cmake --build . --config Release --target install || goto :error
cd ../../..

cd third_party/protobuf/cmake
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% -DZLIB_ROOT=%INSTALL_DIR% -Dprotobuf_MSVC_STATIC_RUNTIME=OFF -Dprotobuf_BUILD_TESTS=OFF ..
cmake --build . --config Release --target install || goto :error
cd ../../../..

cd third_party/cares/cares
mkdir cmake
cd cmake
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ..
cmake --build . --config Release --target install || goto :error
cd ../../../..

@rem OpenSSL-Win32 and OpenSSL-Win64 can be downloaded from https://slproweb.com/products/Win32OpenSSL.html
cd cmake
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% -DOPENSSL_ROOT_DIR=%OPENSSL_DIR% -DZLIB_ROOT=%INSTALL_DIR% -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DgRPC_PROTOBUF_PROVIDER=package -DgRPC_ZLIB_PROVIDER=package -DgRPC_CARES_PROVIDER=package -DgRPC_SSL_PROVIDER=package -DCMAKE_BUILD_TYPE=Release ../.. || goto :error
cmake --build . --config Release --target install || goto :error
cd ../..

@rem Build helloworld example using cmake
cd examples/cpp/helloworld
mkdir cmake
cd cmake
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% -DOPENSSL_ROOT_DIR=%OPENSSL_DIR% -DZLIB_ROOT=%INSTALL_DIR% ../.. || goto :error
cmake --build . --config Release || goto :error
cd ../../../../..

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
