gRPC Python CSM Observability
=============================

Package for gRPC Python CSM Observability.

Supported Python Versions
-------------------------
Python >= 3.8

Installation
------------

Currently gRPC Python CSM Observability is **only available for Linux**.

Installing From PyPI
~~~~~~~~~~~~~~~~~~~~

::

  $ pip install grpcio-csm-observability


Installing From Source
~~~~~~~~~~~~~~~~~~~~~~

::

  $ export REPO_ROOT=grpc  # REPO_ROOT can be any directory of your choice
  $ git clone -b RELEASE_TAG_HERE https://github.com/grpc/grpc $REPO_ROOT
  $ cd $REPO_ROOT
  $ git submodule update --init

  $ cd src/python/grpcio_csm_observability

  # For the next command do `sudo pip install` if you get permission-denied errors
  $ pip install .


Dependencies
------------
gRPC Python CSM Observability Depends on the following packages:

::

  grpcio
  grpcio-observability
  opentelemetry-sdk


Usage
-----

Example usage is similar to `the example here <https://github.com/grpc/grpc/tree/master/examples/python/observability>`_, instead of importing from ``grpc_observability``, you should import from ``grpc_csm_observability``:

.. code-block:: python

    import grpc_csm_observability
    
    csm_otel_plugin = grpc_csm_observability.CsmOpenTelemetryPlugin(
        meter_provider=provider
    )


We also provide several environment variables to help you optimize gRPC python observability for your particular use.

* Note: The term "Census" here is just for historical backwards compatibility reasons and does not imply any dependencies.

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
