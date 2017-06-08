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

@rem Convenience script to build gRPC protoc plugins from command line. protoc plugins are used to generate service stub code from .proto service defintions.

setlocal

@rem enter this directory
cd /d %~dp0

@rem Set VS variables (uses Visual Studio 2013)
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem Build third_party/protobuf
msbuild ..\third_party\protobuf\cmake\build\solution\protobuf.sln /p:Configuration=Release || goto :error

@rem Build the C# protoc plugins
msbuild grpc_protoc_plugins.sln /p:Configuration=Release || goto :error

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
