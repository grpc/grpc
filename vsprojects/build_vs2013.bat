@rem Convenience wrapper that runs specified gRPC target using msbuild
@rem Usage: build_vs2013.bat TARGET_NAME

setlocal
@rem Set VS variables (uses Visual Studio 2013)
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

msbuild %*
exit /b %ERRORLEVEL%
endlocal
