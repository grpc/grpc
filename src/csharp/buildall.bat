@rem Convenience script to build gRPC C# from command line

setlocal

@rem enter this directory
cd /d %~dp0

@rem Set VS variables (uses Visual Studio 2013)
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem Build the C# native extension
msbuild ..\..\vsprojects\grpc.sln /t:grpc_csharp_ext /p:PlatformToolset=v120 || goto :error

msbuild Grpc.sln /p:Configuration=Debug || goto :error
msbuild Grpc.sln /p:Configuration=Release || goto :error

if "%1" == "BUILD_SIGNED" (
msbuild Grpc.sln /p:Configuration=ReleaseSigned || goto :error
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
