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

@rem Download OpenSSL-Win32 originally installed from https://slproweb.com/products/Win32OpenSSL.html
powershell -Command "(New-Object Net.WebClient).DownloadFile('https://storage.googleapis.com/grpc-testing.appspot.com/OpenSSL-Win32-1_1_0g.zip', 'OpenSSL-Win32.zip')"
powershell -Command "Add-Type -Assembly 'System.IO.Compression.FileSystem'; [System.IO.Compression.ZipFile]::ExtractToDirectory('OpenSSL-Win32.zip', '.');"

@rem set absolute path to OpenSSL with forward slashes
set OPENSSL_DIR=%cd:\=/%/OpenSSL-Win32

@rem Build helloworld example using cmake
@rem Use non-standard build directory to avoid too long filenames
mkdir example_build
cd example_build
cmake -DOPENSSL_ROOT_DIR=%OPENSSL_DIR% ../examples/cpp/helloworld/cmake_externalproject || goto :error
cmake --build . --config Release || goto :error
cd ..

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
