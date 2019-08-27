## Data transmission demo for using gRPC in Python

在Python中使用gRPC时, 进行数据传输的四种方式。

- #### 简单模式 

  没啥好说的,跟调普通方法没差

  `client.py - line:13 - simple_method`

  `server.py - line:17 - SimpleMethod`

- #### 客户端流模式

  在一次调用中, 客户端可以多次向服务器传输数据, 但是服务器只能返回一次响应.

  `clien.py - line:24 - client_streaming_method `

  `server.py - line:25 - ClientStreamingMethod`

- #### 服务端流模式 

  在一次调用中, 客户端只能一次向服务器传输数据, 但是服务器可以多次返回响应

  `clien.py - line:42 - server_streaming_method`

  `server.py - line:35 - ServerStreamingMethod`

- #### 双向流模式

  在一次调用中, 客户端和服务器都可以向对方多次收发数据

  `client.py - line:55 - bidirectional_streaming_method`

  `server.py - line:51 - BidirectionalStreamingMethod`

