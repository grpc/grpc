gRPC Python Observability
=========================

Package for gRPC Python Observability.

More details can be found in `OpenTelemetry Metrics gRFC <https://github.com/grpc/proposal/blob/master/A66-otel-stats.md#opentelemetry-metrics>`_.

How gRPC Python Observability Works
-----------------------------------

gRPC Python is a wrapper layer built upon the gRPC Core (written in C/C++). Most of telemetry data
is collected at core layer and then exported to Python layer. To optimize performance and reduce
the overhead of acquiring the GIL too frequently, telemetry data is initially cached at the Core layer
and then exported to the Python layer in batches.

Note that while this approach enhances efficiency, it will introduce a slight delay between the
time the data is collected and the time it becomes available through Python exporters.


Supported Python Versions
-------------------------
Python >= 3.7

Installation
------------

Currently gRPC Python Observability is **only available for Linux**.

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
  opentelemetry-api==1.21.0


Usage
-----

You can find example usage in `Python example folder <https://github.com/grpc/grpc/tree/master/examples/python/observability>`_.

We also provide several environment variables to help you optimize gRPC python observability for your particular use.

1. GRPC_PYTHON_CENSUS_EXPORT_BATCH_INTERVAL
    * This controls how frequently telemetry data collected within gRPC Core is sent to Python layer.
    * Default value is 0.5 (Seconds).

2. GRPC_PYTHON_CENSUS_MAX_EXPORT_BUFFER_SIZE
    * This controls the maximum number of telemetry data items that can be held in the buffer within gRPC Core before they are sent to Python.
    * Default value is 10,000.

3. GRPC_PYTHON_CENSUS_EXPORT_THRESHOLD
    * This setting acts as a trigger: When the buffer in gRPC Core reaches a certain percentage of its capacity, the telemetry data is sent to Python.
    * Default value is 0.7 (Which means buffer will start export when it's 70% full).

4. GRPC_PYTHON_CENSUS_EXPORT_THREAD_TIMEOUT
    * This controls the maximum time allowed for the exporting thread (responsible for sending data to Python) to complete.
    * Main thread will terminate the exporting thread after this timeout.
    * Default value is 10 (Seconds).
