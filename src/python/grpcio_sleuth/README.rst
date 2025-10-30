gRPC Python Sleuth Package
==========================

This package contains the gRPC Sleuth for Python. Sleuth is a command-line tool
to debug gRPC applications at runtime. See //test/cpp/sleuth for more details.

Installation
------------

Currently, gRPC Sleuth is only available for Linux on x86.

Installing From PyPI
~~~~~~~~~~~~~~~~~~~~

::

  $ pip install grpcio-sleuth


Installing From Source
~~~~~~~~~~~~~~~~~~~~~~

Building from source requires Bazel.

::

  $ export REPO_ROOT=grpc  # REPO_ROOT can be any directory of your choice
  $ git clone -b RELEASE_TAG_HERE https://github.com/grpc/grpc $REPO_ROOT
  $ cd $REPO_ROOT
  $ cd src/python/grpcio_sleuth
  $ pip install .


Usage
-----

You can run the gRPC Sleuth CLI:

::

  $ grpc_sleuth help

You can also call it programmatically.
Run this outside of src/python/grpcio_sleuth to avoid import issues.

::

  $ python <<EOF
  from grpc_sleuth import sleuth_lib
  sleuth_lib.run_sleuth(['info'], print)
  EOF
  Sleuth version 1761265538

