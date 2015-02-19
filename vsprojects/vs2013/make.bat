@rem Convenience wrapper that runs specified gRPC target using Nmake
@rem Usage: make.bat TARGET_NAME

@rem Set VS variables
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

nmake.exe /f Grpc.mak %1