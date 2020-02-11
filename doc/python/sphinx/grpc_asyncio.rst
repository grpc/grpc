gRPC AsyncIO API
================

.. module:: grpc.experimental.aio

Overview
--------

gRPC AsyncIO API is a new version of gRPC Python whose architecture is
tailored to AsyncIO. Underlying, it is using C-Core's callback API, and
replaced all IO operations with methods provided by the AsyncIO library.

This stack currently is under active development. Feel free to offer
suggestions by opening issues on `grpc/grpc <https://github.com/grpc/grpc>`_.

The design doc can be found here as `gRFC <https://github.com/grpc/proposal/pull/155>`_.


Module Contents
---------------


Create Client
^^^^^^^^^^^^^

.. autofunction:: insecure_channel
.. autofunction:: secure_channel


Create Server
^^^^^^^^^^^^^

.. autofunction:: server


Channel Object
^^^^^^^^^^^^^^

.. autoclass:: Channel


Server Object
^^^^^^^^^^^^^

.. autoclass:: Server


gRPC Exceptions
^^^^^^^^^^^^^^^

.. autoexception:: BaseError
.. autoexception:: AioRpcError
.. autoexception:: UsageError
.. autoexception:: AbortError
.. autoexception:: InternalError


Client-Side Context
^^^^^^^^^^^^^^^^^^^^^^^

.. autoclass:: Call
.. autoclass:: UnaryUnaryCall
.. autoclass:: UnaryStreamCall
.. autoclass:: StreamUnaryCall
.. autoclass:: StreamStreamCall


Client-Side Interceptor
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. autoclass:: ClientCallDetails
.. autoclass:: InterceptedUnaryUnaryCall
.. autoclass:: UnaryUnaryClientInterceptor

.. Service-Side Context
.. ^^^^^^^^^^^^^^^^^^^^

.. .. autoclass:: ServicerContext


.. Service-Side Interceptor
.. ^^^^^^^^^^^^^^^^^^^^^^^^

.. .. autoclass:: ServerInterceptor


Multi-Callable Interfaces
^^^^^^^^^^^^^^^^^^^^^^^^^

.. autoclass:: UnaryUnaryMultiCallable()
.. autoclass:: UnaryStreamMultiCallable()
.. autoclass:: StreamUnaryMultiCallable()
.. autoclass:: StreamStreamMultiCallable()


.. Future Interfaces
.. ^^^^^^^^^^^^^^^^^

.. .. autoexception:: FutureTimeoutError
.. .. autoexception:: FutureCancelledError
.. .. autoclass:: Future


.. Compression
.. ^^^^^^^^^^^

.. .. autoclass:: Compression
