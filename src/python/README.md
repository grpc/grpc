GRPC Python
=========

The Python facility of GRPC.


Prerequisites
-----------------------

Python 2.7, virtualenv, pip, libprotobuf-dev, and libprotoc-dev.


Building from source
----------------------

- Build the GRPC core
E.g, from the root of the grpc [git repo](https://github.com/google/grpc)
```
$ make shared_c static_c
```

- Use build_python.sh to build the Python code and install it into a virtual environment
```
$ tools/run_tests/build_python.sh
```


Testing
-----------------------

- Use run_python.sh to run GRPC as it was installed into the virtual environment
```
$ tools/run_tests/run_python.sh
```
