@rem Builds gRPC NuGet packages

@rem Current package versions
set VERSION=0.6.0
set CORE_VERSION=0.10.0

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe

setlocal
cd ..\..\vsprojects\nuget_package
@call buildall.bat || goto :error
endlocal

@call buildall.bat || goto :error

%NUGET% pack ..\..\vsprojects\nuget_package\grpc.native.csharp_ext.nuspec -Version %CORE_VERSION% || goto :error
%NUGET% pack Grpc.Auth\Grpc.Auth.nuspec -Symbols -Version %VERSION% || goto :error
%NUGET% pack Grpc.Core\Grpc.Core.nuspec -Symbols -Version %VERSION% -Properties GrpcNativeCsharpExtVersion=%CORE_VERSION% || goto :error
%NUGET% pack Grpc.HealthCheck\Grpc.HealthCheck.nuspec -Symbols -Version %VERSION% || goto :error
%NUGET% pack Grpc.Tools.nuspec -Version %VERSION% || goto :error
%NUGET% pack Grpc.nuspec -Version %VERSION% || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
