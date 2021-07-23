gRPC Services as DLLs : Bug
===========================

To reproduce this bug, run the following script in this directory (in Git Bash):

```
./build-dlls.sh
```

Then go to Visual Studio solution in cmake/build dir and open the Visual Studio solution.

Then you can right click on a specific project (greeter_server and greeter_client) to run those executables. Note: run `other_service` build first to get the service as DLL.

You can open a second instance of Visual Studio, and in one instance you Debug `greeter_server` and in another you run Debug `greeter_client`.

The bug that you will see is that the reply seems to trigger an error:

```
Exception thrown: read access violation.
**grpc::g_core_codegen_interface** was nullptr.
```

Note: the compiler warnings about needing to have dll-interfaces are explained here: https://github.com/protocolbuffers/protobuf/blob/master/cmake/README.md#notes-on-compiler-warnings

Note: I added some flags about letting protobuf libs be used as DLLs, but if you remove them, it doesn't make any difference. I thought this might fix the error or at least change the error. See: https://github.com/protocolbuffers/protobuf/blob/master/cmake/README.md#dlls-vs-static-linking
