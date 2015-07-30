@rem Convenience wrapper that runs specified gRPC target using Nmake
@rem Usage: make.bat TARGET_NAME

setlocal
@rem Set VS variables (uses Visual Studio 2013)
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem /K: continue on error
nmake /K /f Grpc.mak %*
exit /b %ERRORLEVEL%
endlocal
