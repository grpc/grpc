@rem Convenience script to build gRPC C# from command line

setlocal
@rem Set VS variables (uses Visual Studio 2013)
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem Build the C# native extension
msbuild ..\..\vsprojects\grpc.sln /t:grpc_csharp_ext || goto :error

msbuild Grpc.sln /p:Configuration=Debug || goto :error
msbuild Grpc.sln /p:Configuration=Release || goto :error
endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
