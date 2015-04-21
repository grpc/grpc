This directory contains MS Visual Studio project & solution files.

#Supported Visual Studio versions

Currently supported versions are Visual Studio 2013 (primary), 2012 and 2010.

#Building
We are using nuget to pull zlib and openssl dependencies.
If you don't have Visual Studio NuGet plugin installed, you'll need to
manually restore the NuGet packages.

```
REM no need to do this if you have NuGet visual studio extension.
nuget restore
```

After that, open `grpc.sln` with Visual Studio and hit "Build".

#Testing

Use make.bat to build are run gRPC tests.
```
make.bat test
```


