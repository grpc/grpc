gRPC AsyncIO API
================

.. module:: grpc.experimental.aio

Overview
--------

gRPC AsyncIO API is the **new version** of gRPC Python whose architecture is
tailored to AsyncIO. Underlying, it utilizes the same C-extension, gRPC C-Core,
as existing stack, and it replaces all gRPC IO operations with methods provided
by the AsyncIO library.

This stack currently is under active development. Feel free to offer
suggestions by opening issues on our GitHub repo `grpc/grpc <https://github.com/grpc/grpc>`_.

The design doc can be found here as `gRFC <https://github.com/grpc/proposal/pull/155>`_.


Caveats
-------

gRPC Async API objects may only be used on the thread on which they were
created. AsyncIO doesn't provide thread safety for most of its APIs.


Module Contents
---------------

Enable AsyncIO in gRPC
^^^^^^^^^^^^^^^^^^^^^^

.. function:: init_grpc_aio

    Enable AsyncIO for gRPC Python.

    This function is idempotent and it should be invoked before creation of
    AsyncIO stack objects. Otherwise, the application might deadlock.

    This function configurates the gRPC C-Core to invoke AsyncIO methods for IO
    operations (e.g., socket read, write). The configuration applies to the
    entire process.

    After invoking this function, making blocking function calls in coroutines
    or in the thread running event loop will block the event loop, potentially
    starving all RPCs in the process. Refer to the Python language
    documentation on AsyncIO for more details (`running-blocking-code <https://docs.python.org/3/library/asyncio-dev.html#running-blocking-code>`_).


Create Channel
^^^^^^^^^^^^^^

Channels are the abstraction of clients, where most of networking logic
happens, for example, managing one or more underlying connections, name
resolution, load balancing, flow control, etc.. If you are using ProtoBuf,
Channel objects works best when further encapsulate into stub objects, then the
application can invoke remote functions as if they are local functions.

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
