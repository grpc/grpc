@rem Builds gRPC NuGet packages

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe

setlocal
cd ..\..\vsprojects\nuget_package
@call buildall.bat || goto :error
endlocal

@call buildall.bat || goto :error

%NUGET% pack ..\..\vsprojects\nuget_package\grpc.native.csharp_ext.nuspec || goto :error
%NUGET% pack Grpc.Core\Grpc.Core.nuspec || goto :error
%NUGET% pack Grpc.Auth\Grpc.Auth.nuspec || goto :error
%NUGET% pack Grpc.nuspec || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
