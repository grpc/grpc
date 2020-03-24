## Data transmission demo for using gRPC in Python

在Python中使用gRPC时, 进行数据传输的四种方式  [官方指南](<https://grpc.io/docs/guides/concepts/#unary-rpc>)

- #### 一元模式

  在一次调用中, 客户端只能向服务器传输一次请求数据, 服务器也只能返回一次响应

  `client.py: simple_method`

  `server.py: SimpleMethod`

- #### 客户端流模式

  在一次调用中, 客户端可以多次向服务器传输数据, 但是服务器只能返回一次响应

  `client.py: client_streaming_method `

  `server.py: ClientStreamingMethod`

- #### 服务端流模式 

  在一次调用中, 客户端只能向服务器传输一次请求数据, 但是服务器可以多次返回响应

  `client.py: server_streaming_method`

  `server.py: ServerStreamingMethod`

- #### 双向流模式

  在一次调用中, 客户端和服务器都可以向对方多次收发数据

  `client.py: bidirectional_streaming_method`

  `server.py: BidirectionalStreamingMethod`

