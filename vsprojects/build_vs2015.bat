@rem Convenience wrapper that runs specified gRPC target using msbuild
@rem Usage: build.bat TARGET_NAME

setlocal
@rem Set VS variables (uses Visual Studio 2015)
@call "%VS140COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

msbuild %*
exit /b %ERRORLEVEL%
endlocal
