This directory contains MS Visual Studio project & solution files.

#Supported Visual Studio versions

Currently supported versions are Visual Studio 2013 (our primary focus) and 2010.

#Building
We are using [NuGet](http://www.nuget.org) to pull zlib and openssl dependencies.
If you don't have Visual Studio NuGet plugin installed, you'll need to
download nuget.exe from the web and manually restore the NuGet packages.

```
> REM Run from this directory.
> REM No need to do this if you have NuGet visual studio extension.
> nuget restore
```

After that, you can build the solution using one of these options:
1. open `grpc.sln` with Visual Studio and hit "Build".
2. build from commandline using `msbuild grpc.sln /p:Configuration=Debug`

#Testing

Use `run_tests.py`, that also supports Windows (with a bit limited experience).
```
> REM Run from repository root.
> python tools\run_tests\run_tests.py -l c
```

Also, you can `make.bat` directly to build and run gRPC tests.
```
> REM Run from this directory.
> make.bat alarm_test
```

# Building protoc plugins
For generating service stub code, gRPC relies on plugins for `protoc` (the protocol buffer compiler). The solution `grpc_protoc_plugins.sln` allows you to build
Windows .exe binaries of gRPC protoc plugins.

1. Open solution `third_party\protobuf\vsprojects\protobuf.sln`
2. Accept the conversion to newer Visual Studio version and ignore errors about gtest.
3. Build libprotoc in Release mode.
4. Open solution `vsprojects\grpc_protoc_plugins.sln` and build it in Release mode. As a result, you should obtain a set of gRPC protoc plugin binaries (`grpc_cpp_plugin.exe`, `grpc_csharp_plugin.exe`, ...)
