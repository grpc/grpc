
To run the example, the following steps are needed:

```
cd examples/helloworld
pushd cmake/build/
cmake -G "Visual Studio 16 2019" -A x64 ../.. -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE
```

Then go to Visual Studio solution in cmake/build dir and build all.

Then you can right click on a specific project (greeter_server and greeter_client) to run those executables.
