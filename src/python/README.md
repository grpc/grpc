gRPC Python
=========

The Python facility of gRPC.


Status
-------

Usable with limitations, Pre-Alpha

Prerequisites
-----------------------

Python 2.7, virtualenv, pip, libprotobuf-dev, and libprotoc-dev.


Building from source
----------------------

- Build the gRPC core from the root of the
  [gRPC git repo](https://github.com/grpc/grpc)
```
$ make shared_c static_c
```

- Use build_python.sh to build the Python code and install it into a virtual environment
```
$ tools/run_tests/build_python.sh
```


Testing
-----------------------

- Use run_python.sh to run gRPC as it was installed into the virtual environment
```
$ tools/run_tests/run_python.sh
```


Installing
-----------------------

- [Install the gRPC core](https://github.com/grpc/grpc/blob/master/INSTALL)

- Install gRPC Python's dependencies
```
$ pip install -r requirements.txt
```

- Install gRPC Python
```
$ pip install src/python/src
```
