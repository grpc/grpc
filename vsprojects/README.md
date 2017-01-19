#Pre-generated MS Visual Studio project & solution files

Versions 2013 and 2015 are both supported. You can use [their respective
community
editions](https://www.visualstudio.com/en-us/downloads/download-visual-studio-vs.aspx).

#Building
We are using [NuGet](http://www.nuget.org) to pull zlib and openssl dependencies.
If you don't have Visual Studio NuGet plugin installed, you'll need to
download nuget.exe from the web and manually restore the NuGet packages.

```
> REM Run from this directory.
> REM No need to do this if you have NuGet visual studio extension.
> nuget restore grpc.sln
```

After that, you can build the solution using one of these options:
  1. open `grpc.sln` with Visual Studio and hit "Build".
  2. build from commandline using `msbuild grpc.sln /p:Configuration=Debug`

#C/C++ Test Dependencies
   * gtest isn't available as a git repo like the other dependencies.  download it and add it to `/third_party/gtest/` (the folder will end up with `/build-aux/`, `/cmake/`, `/codegear/`, etc. folders in it).  
    * if using vs2013: open/import the gtest solution in `/msvc/`, and save over the first solution (you will have to change it from read-only).  change all projects to use `/MDd` (Property Pages - C/C++ - Code Generation - Runtime Library) and build. This is a "multithreaded debug" setting and it needs to match grpc.
    * build all
   * open protobuf solution in `/third_party/protobuf/vsprojects`
    * if using vs2013: on import the gtest stuff will probably fail, I think the paths are interpreted wrong.  it's ok.
    * tests and test_plugin will fail when built.  also ok
    * build all
   *  gflags is automatically imported as a git submodule but it needs to have CMake run on it to be ready for a specific platform
    * download [CMake](http://www.cmake.org/) windows installer; install
    * open visual studio developer command prompt (not sure if dev command prompt is necessary)
    * run `cmake <path to gtest directory>`
    * this will build a `.sln` and fill up the `/third_party/gflags/include/gflags/` directory with headers
    * build all
   * install [NuGet](http://www.nuget.org)
    * nuget should automatically bring in built versions of zlib and openssl when building grpc.sln (the versions in `/third_party/` are not used).  If it doesn't work use `tools->nuget...->manage...`.  The packages are put in `/vsprojects/packages/`

# Building protoc plugins
For generating service stub code, gRPC relies on plugins for `protoc` (the protocol buffer compiler). The solution `grpc_protoc_plugins.sln` allows you to build
Windows .exe binaries of gRPC protoc plugins.

- Follow instructions in `third_party\protobuf\cmake\README.md` to create Visual Studio 2013 projects for protobuf.
  ```
  $ cd third_party/protobuf/cmake
  $ mkdir build & cd build
  $ mkdir solution & cd solution
  $ cmake -G "Visual Studio 12 2013" -Dprotobuf_BUILD_TESTS=OFF ../..
  ```

- Open solution `third_party\protobuf\cmake\build\solution\protobuf.sln` and build it in Release mode. That will build libraries `libprotobuf.lib` and `libprotoc.lib` needed for the next step.

- Open solution `vsprojects\grpc_protoc_plugins.sln` and build it in Release mode. As a result, you should obtain a set of gRPC protoc plugin binaries (`grpc_cpp_plugin.exe`, `grpc_csharp_plugin.exe`, ...)
