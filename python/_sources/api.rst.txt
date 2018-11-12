API Reference
=============

.. module:: grpc

Create Client
-------------

.. autofunction:: insecure_channel
.. autofunction:: secure_channel
.. autofunction:: intercept_channel


Create Client Credentials
-------------------------

.. autofunction:: ssl_channel_credentials
.. autofunction:: metadata_call_credentials
.. autofunction:: access_token_call_credentials
.. autofunction:: composite_call_credentials
.. autofunction:: composite_channel_credentials


Create Server
-------------

.. autofunction:: server


Create Server Credentials
-------------------------

.. autofunction:: ssl_server_credentials
.. autofunction:: ssl_server_certificate_configuration
.. autofunction:: dynamic_ssl_server_credentials


RPC Method Handlers
--------------------------

.. autofunction:: unary_unary_rpc_method_handler
.. autofunction:: unary_stream_rpc_method_handler
.. autofunction:: stream_unary_rpc_method_handler
.. autofunction:: stream_stream_rpc_method_handler
.. autofunction:: method_handlers_generic_handler


Channel Ready Future
--------------------------

.. autofunction:: channel_ready_future


Channel Connectivity
--------------------------

.. autoclass:: ChannelConnectivity


gRPC Status Code
--------------------------

.. autoclass:: StatusCode


Channel Object
--------------

.. autoclass:: Channel


Server Object
-------------

.. autoclass:: Server


Authentication & Authorization Objects
--------------------------------------

.. autoclass:: ChannelCredentials
.. autoclass:: CallCredentials
.. autoclass:: AuthMetadataContext
.. autoclass:: AuthMetadataPluginCallback
.. autoclass:: AuthMetadataPlugin
.. autoclass:: ServerCredentials
.. autoclass:: ServerCertificateConfiguration


gRPC Exceptions
---------------

.. autoexception:: RpcError


Shared Context
--------------

.. autoclass:: RpcContext


Client-Side Context
-----------------------

.. autoclass:: Call


Client-Side Interceptor
------------------------------------------------

.. autoclass:: ClientCallDetails
.. autoclass:: UnaryUnaryClientInterceptor
.. autoclass:: UnaryStreamClientInterceptor
.. autoclass:: StreamUnaryClientInterceptor
.. autoclass:: StreamStreamClientInterceptor


Service-Side Context
--------------------

.. autoclass:: ServicerContext


Service-Side Handler
-------------------------------

.. autoclass:: RpcMethodHandler
.. autoclass:: HandlerCallDetails
.. autoclass:: GenericRpcHandler
.. autoclass:: ServiceRpcHandler


Service-Side Interceptor
------------------------

.. autoclass:: ServerInterceptor


Multi-Callable
-------------------------

.. autoclass:: UnaryUnaryMultiCallable
.. autoclass:: UnaryStreamMultiCallable
.. autoclass:: StreamUnaryMultiCallable
.. autoclass:: StreamStreamMultiCallable


Future
----------------

.. autoexception:: FutureTimeoutError
.. autoexception:: FutureCancelledError
.. autoclass:: Future
