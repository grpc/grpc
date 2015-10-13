@rem Builds gRPC NuGet packages

@rem Current package versions
set VERSION=0.7.1
set CORE_VERSION=0.11.1
set PROTOBUF_VERSION=3.0.0-alpha4

@rem Packages that depend on prerelease packages (like Google.Protobuf) need to have prerelease suffix as well.
set VERSION_WITH_BETA=%VERSION%-beta

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe

setlocal
cd ..\..\vsprojects\nuget_package
@call buildall.bat || goto :error
endlocal

@call buildall.bat BUILD_SIGNED || goto :error

@call ..\..\vsprojects\build_plugins.bat || goto :error

%NUGET% pack ..\..\vsprojects\nuget_package\grpc.native.csharp.nuspec -Version %CORE_VERSION% || goto :error
%NUGET% pack Grpc.Auth\Grpc.Auth.nuspec -Symbols -Version %VERSION% || goto :error
%NUGET% pack Grpc.Core\Grpc.Core.nuspec -Symbols -Version %VERSION% -Properties GrpcNativeCsharpVersion=%CORE_VERSION% || goto :error
%NUGET% pack Grpc.HealthCheck\Grpc.HealthCheck.nuspec -Symbols -Version %VERSION_WITH_BETA% -Properties ProtobufVersion=%PROTOBUF_VERSION% || goto :error
%NUGET% pack Grpc.Tools.nuspec -Version %VERSION% || goto :error
%NUGET% pack Grpc.nuspec -Version %VERSION% || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
