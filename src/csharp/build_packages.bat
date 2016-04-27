@rem Builds gRPC NuGet packages

@rem Current package versions
set VERSION=0.14.0-dev
set PROTOBUF_VERSION=3.0.0-beta2

@rem Packages that depend on prerelease packages (like Google.Protobuf) need to have prerelease suffix as well.
set VERSION_WITH_BETA=%VERSION%-beta

@rem Adjust the location of nuget.exe
set NUGET=C:\nuget\nuget.exe

@rem Collect the artifacts built by the previous build step if running on Jenkins
@rem TODO(jtattermusch): is there a better way to do this?
xcopy /Y /I ..\..\architecture=x86,language=csharp,platform=windows\artifacts\* Grpc.Core\windows_x86\
xcopy /Y /I ..\..\architecture=x64,language=csharp,platform=windows\artifacts\* Grpc.Core\windows_x64\
xcopy /Y /I ..\..\architecture=x86,language=csharp,platform=linux\artifacts\* Grpc.Core\linux_x86\
xcopy /Y /I ..\..\architecture=x64,language=csharp,platform=linux\artifacts\* Grpc.Core\linux_x64\
xcopy /Y /I ..\..\architecture=x86,language=csharp,platform=macos\artifacts\* Grpc.Core\macosx_x86\
xcopy /Y /I ..\..\architecture=x64,language=csharp,platform=macos\artifacts\* Grpc.Core\macosx_x64\

@rem Collect protoc artifacts built by the previous build step
xcopy /Y /I ..\..\architecture=x86,language=protoc,platform=windows\artifacts\* protoc_plugins\windows_x86\
xcopy /Y /I ..\..\architecture=x64,language=protoc,platform=windows\artifacts\* protoc_plugins\windows_x64\
xcopy /Y /I ..\..\architecture=x86,language=protoc,platform=linux\artifacts\* protoc_plugins\linux_x86\
xcopy /Y /I ..\..\architecture=x64,language=protoc,platform=linux\artifacts\* protoc_plugins\linux_x64\
xcopy /Y /I ..\..\architecture=x86,language=protoc,platform=macos\artifacts\* protoc_plugins\macosx_x86\
xcopy /Y /I ..\..\architecture=x64,language=protoc,platform=macos\artifacts\* protoc_plugins\macosx_x64\

@rem Fetch all dependencies
%NUGET% restore ..\..\vsprojects\grpc_csharp_ext.sln || goto :error
%NUGET% restore Grpc.sln || goto :error

setlocal

@call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" x86

@rem We won't use the native libraries from this step, but without this Grpc.sln will fail.
msbuild ..\..\vsprojects\grpc_csharp_ext.sln /p:Configuration=Release /p:PlatformToolset=v120 || goto :error

msbuild Grpc.sln /p:Configuration=ReleaseSigned || goto :error

endlocal

%NUGET% pack Grpc.Auth\Grpc.Auth.nuspec -Symbols -Version %VERSION% || goto :error
%NUGET% pack Grpc.Core\Grpc.Core.nuspec -Symbols -Version %VERSION% || goto :error
%NUGET% pack Grpc.HealthCheck\Grpc.HealthCheck.nuspec -Symbols -Version %VERSION_WITH_BETA% -Properties ProtobufVersion=%PROTOBUF_VERSION% || goto :error
%NUGET% pack Grpc.nuspec -Version %VERSION% || goto :error
%NUGET% pack Grpc.Tools.nuspec -Version %VERSION% || goto :error

@rem copy resulting nuget packages to artifacts directory
xcopy /Y /I *.nupkg ..\..\artifacts\

@rem create a zipfile with the artifacts as well
powershell -Command "Add-Type -Assembly 'System.IO.Compression.FileSystem'; [System.IO.Compression.ZipFile]::CreateFromDirectory('..\..\artifacts', 'csharp_nugets.zip');"
xcopy /Y /I csharp_nugets.zip ..\..\artifacts\

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
