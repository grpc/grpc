@rem Convenience wrapper that runs specified gRPC target using Nmake
@rem Usage: make.bat TARGET_NAME

setlocal
@rem Set VS variables
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

nmake.exe /f Grpc.mak %1
endlocal