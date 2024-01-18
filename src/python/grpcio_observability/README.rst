gRPC Python Observability
===========
Package for gRPC Python Observability.

More details can be found in `OpenTelemetry Metrics gRFC <https://github.com/grpc/proposal/blob/master/A66-otel-stats.md#opentelemetry-metrics>`_.

Supported Python Versions
-------------------------
Python >= 3.7

Installation
------------

Currently gRPC Python Observability is :code:`only available for Linux`.

Installing From PyPI
~~~~~~~~~~~~~~~~~~~~

::

  $ pip install grpcio-observability


Installing From Source
~~~~~~~~~~~~~~~~~~~~~~

Building from source requires that you have the Python headers (usually a
package named :code:`python-dev`) and Cython installed. It further requires a
GCC-like compiler to go smoothly; you can probably get it to work without
GCC-like stuff, but you may end up having a bad time.

::

  $ export REPO_ROOT=grpc  # REPO_ROOT can be any directory of your choice
  $ git clone -b RELEASE_TAG_HERE https://github.com/grpc/grpc $REPO_ROOT
  $ cd $REPO_ROOT
  $ git submodule update --init

  $ cd src/python/grpcio_observability
  $ python -m make_grpcio_observability

  # For the next command do `sudo pip install` if you get permission-denied errors
  $ GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .


Dependencies
-------------------------
gRPC Python Observability Depends on the following packages:

::

  grpcio
  opentelemetry-sdk==1.21.0
  opentelemetry-api==1.21.0


Usage
-----

You can find example usage in `Python example folder <https://pypi.python.org/pypi/grpcio-health-checking>`_.
