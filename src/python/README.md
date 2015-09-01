gRPC Python
=========
The Python facility of gRPC.

Status
-------
Alpha : Ready for early adopters

PREREQUISITES
-------------
- Python 2.7, virtualenv, pip
- [homebrew][] on Mac OS X.  These simplify the installation of the gRPC C core.

INSTALLATION
-------------

**Linux (Debian):**

Add [Debian testing][] to your `sources.list` file. Example:

```sh
echo "deb http://ftp.us.debian.org/debian testing main contrib non-free" | \
sudo tee -a /etc/apt/sources.list
```

Install the gRPC Debian package

```sh
sudo apt-get update
sudo apt-get install libgrpc-dev
```

Install the gRPC Python module

```sh
sudo pip install grpcio
```

**Mac OS X**

Install [homebrew][]. Run the following command to install gRPC Python.
```sh
$ curl -fsSL https://goo.gl/getgrpc | bash -s python
```
This will download and run the [gRPC install script][], then install the latest version of the gRPC Python package.  It also installs the Protocol Buffers compiler (_protoc_) and the gRPC _protoc_ plugin for python.

EXAMPLES
--------
Please read our online documentation for a [Quick Start][] and a [detailed example][]

BUILDING FROM SOURCE
---------------------
- Clone this repository

- Initialize the git submodules
```
$ git submodule update --init
```

- Make the libraries
```
$ make
```

- Use build_python.sh to build the Python code and install it into a virtual environment
```
$ CONFIG=opt tools/run_tests/build_python.sh 2.7
```

TESTING
-------

- Use run_python.sh to run gRPC as it was installed into the virtual environment
```
$ CONFIG=opt PYVER=2.7 tools/run_tests/run_python.sh
```

PACKAGING
---------

- Install packaging dependencies
```
$ pip install setuptools twine
```

- Push to PyPI
```
$ ../../tools/distrib/python/submit.py
```

[homebrew]:http://brew.sh
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[Quick Start]:http://www.grpc.io/docs/tutorials/basic/python.html
[detailed example]:http://www.grpc.io/docs/installation/python.html
[Debian testing]:https://www.debian.org/releases/stretch/
