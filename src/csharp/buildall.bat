@rem Convenience script to build gRPC C# from command line

setlocal

@rem enter this directory
cd /d %~dp0

@rem Set VS variables (uses Visual Studio 2013)
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem Fetch all dependencies
nuget restore ..\..\vsprojects\grpc.sln || goto :error
nuget restore ..\..\vsprojects\grpc_csharp_ext.sln || goto :error
nuget restore ..\..\vsprojects\grpc_protoc_plugins.sln || goto :error
nuget restore Grpc.sln || goto :error

@rem Build the C# native extension
msbuild ..\..\vsprojects\grpc_csharp_ext.sln /p:Configuration=Debug /p:PlatformToolset=v120 || goto :error
msbuild ..\..\vsprojects\grpc_csharp_ext.sln /p:Configuration=Release /p:PlatformToolset=v120 || goto :error

msbuild Grpc.sln /p:Configuration=Debug || goto :error
msbuild Grpc.sln /p:Configuration=Release || goto :error

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
