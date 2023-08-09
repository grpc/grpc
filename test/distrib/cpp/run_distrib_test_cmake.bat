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

@rem TODO(jtattermusch): add support for GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS env variable

set VS_GENERATOR="Visual Studio 16 2019"
@rem TODO(jtattermusch): switch to x64 build (will require pulling a x64 build of openssl)
set VS_ARCHITECTURE="Win32"

@rem Install absl
mkdir third_party\abseil-cpp\cmake\build
pushd third_party\abseil-cpp\cmake\build
cmake -G %VS_GENERATOR% -A %VS_ARCHITECTURE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ..\..
cmake --build . --config Release --target install || goto :error
popd

@rem Install c-ares
mkdir third_party\cares\cares\cmake\build
pushd third_party\cares\cares\cmake\build
cmake -G %VS_GENERATOR% -A %VS_ARCHITECTURE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ..\..
cmake --build . --config Release --target install || goto :error
popd

@rem Install protobuf
mkdir third_party\protobuf\cmake\build
pushd third_party\protobuf\cmake\build
cmake -G %VS_GENERATOR% -A %VS_ARCHITECTURE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% -Dprotobuf_ABSL_PROVIDER=package -DZLIB_ROOT=%INSTALL_DIR% -Dprotobuf_MSVC_STATIC_RUNTIME=OFF -Dprotobuf_BUILD_TESTS=OFF ..\..
cmake --build . --config Release --target install || goto :error
popd

@rem Install re2
mkdir third_party\re2\cmake\build
pushd third_party\re2\cmake\build
cmake -G %VS_GENERATOR% -A %VS_ARCHITECTURE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ..\..
cmake --build . --config Release --target install || goto :error
popd

@rem Install zlib
mkdir third_party\zlib\cmake\build
pushd third_party\zlib\cmake\build
cmake -G %VS_GENERATOR% -A %VS_ARCHITECTURE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ..\..
cmake --build . --config Release --target install || goto :error
popd

@rem Just before installing gRPC, wipe out contents of all the submodules to simulate
@rem a standalone build from an archive
@rem NOTE(lidiz) We used to use "git submodule deinit", but it leaves an empty
@rem folder for deinit-ed submodules, blocking the CMake download. For users
@rem downloaded gRPC code as an archive, they won't have submodule residual
@rem folders, like the following command trying to imitate.
git submodule foreach bash -c "cd $toplevel; rm -rf $name"

@rem Install gRPC
@rem NOTE(jtattermusch): The -DProtobuf_USE_STATIC_LIBS=ON is necessary on cmake3.16+
@rem since by default "find_package(Protobuf ...)" uses the cmake's builtin
@rem FindProtobuf.cmake module, which now requires the info whether protobuf
@rem is to be linked statically.
@rem See https://github.com/Kitware/CMake/commit/3bbd85d5fffe66181cf16c81b23b2ba50f5387ba
@rem See https://gitlab.kitware.com/cmake/cmake/-/merge_requests/3555#note_660390
mkdir cmake\build
pushd cmake\build
cmake ^
  -G %VS_GENERATOR% ^
  -A %VS_ARCHITECTURE% ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ^
  -DOPENSSL_ROOT_DIR=%OPENSSL_DIR% ^
  -DZLIB_ROOT=%INSTALL_DIR% ^
  -DgRPC_INSTALL=ON ^
  -DgRPC_BUILD_TESTS=OFF ^
  -DgRPC_BUILD_MSVC_MP_COUNT=-1 ^
  -DgRPC_ABSL_PROVIDER=package ^
  -DgRPC_CARES_PROVIDER=package ^
  -DgRPC_PROTOBUF_PROVIDER=package ^
  -DProtobuf_USE_STATIC_LIBS=ON ^
  -DgRPC_RE2_PROVIDER=package ^
  -DgRPC_SSL_PROVIDER=package ^
  -DgRPC_ZLIB_PROVIDER=package ^
  ../.. || goto :error
cmake --build . --config Release --target install || goto :error
popd

@rem Build helloworld example using cmake
mkdir examples\cpp\helloworld\cmake\build
pushd examples\cpp\helloworld\cmake\build
cmake -G %VS_GENERATOR% -A %VS_ARCHITECTURE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% -DOPENSSL_ROOT_DIR=%OPENSSL_DIR% -DZLIB_ROOT=%INSTALL_DIR% ../.. || goto :error
cmake --build . --config Release || goto :error
popd

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
