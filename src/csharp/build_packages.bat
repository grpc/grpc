@rem Builds NuGet packages

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe

@call buildall.bat || goto :error

%NUGET% pack Grpc.Core\Grpc.Core.nuspec || goto :error
%NUGET% pack Grpc.Auth\Grpc.Auth.nuspec || goto :error
%NUGET% pack Grpc.nuspec || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
