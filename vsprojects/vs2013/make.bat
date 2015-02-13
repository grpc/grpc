@rem Convenience wrapper that run specified gRPC target using Nmake
@rem Usage: make.bat TARGET_NAME

@rem Set VS variables
@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

nmake.exe /f GrpcTests.mak %1