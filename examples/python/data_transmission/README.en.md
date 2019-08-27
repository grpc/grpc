##  Data transmission demo for using gRPC in Python

Four ways of data transmission when gRPC is used in Python.

- #### unary-unary

  There's nothing to say. It's no different from the usual way.

  `client.py - line:13 - simple_method`

  `server.py - line:17 - SimpleMethod`
- #### stream-unary

  In a single call, the client can transfer data to the server an arbitrary number of times, but the server can only return a response once.

  `clien.py - line:24 - client_streaming_method`

  `server.py - line:25 - ClientStreamingMethod`

- #### unary-stream

  In a single call, the client can only transmit data to the server at one time, but the server can return the response many times.

  `clien.py - line:42 - server_streaming_method`

  `server.py - line:35 - ServerStreamingMethod`

- #### stream-stream

  In a single call, both client and server can send and receive data 
  to each other multiple times.

  `client.py - line:55 - bidirectional_streaming_method`

  `server.py - line:51 - BidirectionalStreamingMethod`

