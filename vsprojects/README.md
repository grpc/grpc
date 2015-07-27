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

#C/C++ Test Build Steps
   * set up dependencies (above)
   * add `"debug": true,` to the top of build.json.  This is the base file for all build tracking, see [templates](https://github.com/grpc/grpc/tree/master/templates) for more information
    * `"debug": true,` gets picked up by `/tools/buildgen/plugins/generate_vsprojects.py`.  It tells the script to add visual studio GUIDs to all projects.  Otherwise only the projects that already have GUIDs in build.json will be built
   * A basic git version of grpc only has templates for non-test items.  run `/templates/vsprojects/generate_debug_projects.sh` to make debug templates/projects.  This runs a regular visual studio buildgen process, which creates the `.sln` file with all of the new debug projects, then uses git diff to find the new project names from the `.sln` that need templates added.  It builds the new templates based on the diff, then re-runs the visual studio buildgen, which builds the vs projects for each of the new debug targets
    * copy over the `/vsprojects/` folder to your windows build setup (assuming this was built on linux in order to have easy access to python/mako and shell scripts)
   * run `/templates/vsprojects/build_test_protos.sh`
    * this builds all `.proto` files in `/test/` in-place.  there might be a better place to put them that mirrors what happens in the linux build process (todo)
    * each `.proto` file gets built into a `.grpc.pb.cc`, .`grpc.pb.h`, `.pb.cc`, and `.pb.h`.  These are included in each test project in lieu of the `.proto` includes specified in `build.json`.  This substitution is done by `/templates/vsprojects/vcxproj_defs.include`
    * copy over the `/test/` folder in order to get the new files (assuming this was built on linux in order to have an easy protobuf+grpc plugin installation)

#Testing

This is incomplete (only runs some tests for now), todo.  The above .sln-based buildgen makes more tets but isn't tied in to automatic test running yet.

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
