gRPC AsyncIO API
================

.. module:: grpc.experimental.aio

Overview
--------

gRPC AsyncIO API is the **new version** of gRPC Python whose architecture is
tailored to AsyncIO. Underlying, it is using C-Core's callback API, and
replaced all IO operations with methods provided by the AsyncIO library.

This stack currently is under active development. Feel free to offer
suggestions by opening issues on our GitHub repo `grpc/grpc <https://github.com/grpc/grpc>`_.

The design doc can be found here as `gRFC <https://github.com/grpc/proposal/pull/155>`_.


Module Contents
---------------

Turn-On AsyncIO Mode
^^^^^^^^^^^^^^^^^^^^

.. autofunction:: init_grpc_aio


Create Client
^^^^^^^^^^^^^

.. autofunction:: insecure_channel
.. autofunction:: secure_channel


Channel Object
^^^^^^^^^^^^^^

.. autoclass:: Channel


Create Server
^^^^^^^^^^^^^

.. autofunction:: server


Server Object
^^^^^^^^^^^^^

.. autoclass:: Server


gRPC Exceptions
^^^^^^^^^^^^^^^

.. autoexception:: BaseError
.. autoexception:: UsageError
.. autoexception:: AbortError
.. autoexception:: InternalError
.. autoexception:: AioRpcError


Shared Context
^^^^^^^^^^^^^^^^^^^^

.. autoclass:: RpcContext


Client-Side Context
^^^^^^^^^^^^^^^^^^^^^^^

.. autoclass:: Call
.. autoclass:: UnaryUnaryCall
.. autoclass:: UnaryStreamCall
.. autoclass:: StreamUnaryCall
.. autoclass:: StreamStreamCall


Server-Side Context
^^^^^^^^^^^^^^^^^^^^^^^

.. autoclass:: ServicerContext


Client-Side Interceptor
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. autoclass:: ClientCallDetails
.. autoclass:: InterceptedUnaryUnaryCall
.. autoclass:: UnaryUnaryClientInterceptor

.. Service-Side Context
.. ^^^^^^^^^^^^^^^^^^^^

.. .. autoclass:: ServicerContext


Multi-Callable Interfaces
^^^^^^^^^^^^^^^^^^^^^^^^^

.. autoclass:: UnaryUnaryMultiCallable
.. autoclass:: UnaryStreamMultiCallable()
.. autoclass:: StreamUnaryMultiCallable()
.. autoclass:: StreamStreamMultiCallable()
