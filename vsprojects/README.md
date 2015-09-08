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
> nuget restore grpc.sln
```

After that, you can build the solution using one of these options:
1. open `grpc.sln` with Visual Studio and hit "Build".
2. build from commandline using `msbuild grpc.sln /p:Configuration=Debug`

#Making a Project that uses GRPC
Include zlib/OpenSSL libraries and GRPC headers/libraries in your project in the C/C++ option `Additional Include Directories` and Linker option `Additional Library Directories`.  Open grpc.sln and find "edit..." in the drop down for Additional Include Directories for example settings (under "inherited values" which is what the project gets from its .props file).

Gotcha: GRPC is built with `/D "_USE_32BIT_TIME_T"`.  Building a dependent project without this option will cause some odd behavior.  This option has a conflict on VS 2013 and earlier with std::chrono, so if you need std::chrono a possible workaround is to build both library and dependent project with the option removed.


#C++ Test Dependencies
 * open gtest.sln in `/third_party/googletest/msvc`
  * opening this in vs2013 will prompt an upgrade notice
  * build all
 * protobuf is automatically imported as a git submodule but it needs to have CMake run on it to be ready for a specific platform.  See [https://github.com/google/protobuf/tree/master/cmake] for in-depth instructions
  * excerpt:
  * download [CMake](http://www.cmake.org/) windows installer; install
  * open visual studio developer command prompt
  * `cd pth/to/protobuf/cmake`
  * `mkdir build`
  * `cd build`
  * `cmake -G "Visual Studio 12 2013" -DBUILD_TESTING=OFF ..` (type "cmake --help" for a list of build targets)
  * open `/third_party/protobuf/build/protobuf.sln` and make libprotobuf
 * gflags is automatically imported as a git submodule but it needs to have CMake run on it to be ready for a specific platform
  * download [CMake](http://www.cmake.org/) windows installer; install
  * open visual studio developer command prompt
  * run `cmake <path to gflags directory>`
  * this will build a `.sln` and fill up the `/third_party/gflags/include/gflags/` directory with headers
  * build all
 * install [NuGet](http://www.nuget.org) and manually trigger an OpenSSL/zlib import
  * nuget should automatically bring in built versions of zlib and openssl when building grpc.sln (the versions in `/third_party/` are not used)
  * before running run_tests.py, open grpc.sln and initiate a manual build to trigger nuget
  * If it doesn't work use `tools->nuget...->manage...`.  The packages are put in `/vsprojects/packages/`

#C/C++ Test Build Steps
 * C tests are found in `/vsprojects/buildtests_c.sln`.  This file is used by run_tests.py to build tests
 * C++ tests are not yet integrated into a solution file

#Making and running tests with `/tools/run_tests/run_tests.py`
`run_tests.py` builds projects using the .sln files in /vsprojects/

`run_tests.py` options:

 * `run_tests.py --help`
 * `run_tests.py -l c`: run c language tests
 * `run_tests.py -l c++`: run c++ language tests
 * note: `run_tests.py` doesn't normally show build steps, so if a build fails it is best to fall back to building using the solution file

It can be helpful to disable the firewall when running tests so that connection warnings don't pop up.

Individual tests can be run by directly running the executable in `/vsprojects/Relase/` (this is `/bins/opt/` on linux).  Many C tests have no output; they either pass or fail internally and communicate this with their exit code (`0=pass`, `nonzero=fail`)

`run_tests.py` will fail if it can't build something, so not-building tests are disabled for windows in build.yaml.

# Building protoc plugins
For generating service stub code, gRPC relies on plugins for `protoc` (the protocol buffer compiler). The solution `grpc_protoc_plugins.sln` allows you to build
Windows .exe binaries of gRPC protoc plugins.

1. Follow instructions in `third_party\protobuf\cmake\README.md` to create Visual Studio 2013 projects for protobuf.
```
$ cd third_party/protobuf/cmake
$ cmake -G "Visual Studio 12 2013"
```

2. Open solution `third_party\protobuf\cmake\protobuf.sln` and build it in Release mode. That will build libraries `libprotobuf.lib` and `libprotoc.lib` needed for the next step.

3. Open solution `vsprojects\grpc_protoc_plugins.sln` and build it in Release mode. As a result, you should obtain a set of gRPC protoc plugin binaries (`grpc_cpp_plugin.exe`, `grpc_csharp_plugin.exe`, ...)
