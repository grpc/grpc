@rem Convenience script to build gRPC protoc plugins from command line. protoc plugins are used to generate service stub code from .proto service defintions.

setlocal

@rem enter this directory
cd /d %~dp0

@rem Set VS variables (uses Visual Studio 2013)
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem Build third_party/protobuf
msbuild ..\third_party\protobuf\cmake\protobuf.sln /p:Configuration=Release || goto :error

@rem Build the C# protoc plugins
msbuild grpc_protoc_plugins.sln /p:Configuration=Release || goto :error

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
