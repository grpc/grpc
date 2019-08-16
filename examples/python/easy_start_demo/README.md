## Demo for using gRPC in Python

在Python中使用gRPC时, 进行数据传输的四种方式。(Four ways of data transmission when gRPC is used in Python.)

- #### 简单模式 (unary-unary)
```text
没啥好说的,跟调普通方法没差
There's nothing to say. It's no different from the usual way.
```
- #### 客户端流模式 (stream-unary)
```text
在一次调用中, 客户端可以多次向服务器传输数据, 但是服务器只能返回一次响应.
In a single call, the client can transfer data to the server several times,
but the server can only return a response once.
```
- #### 服务端流模式 (unary-stream)
```text
在一次调用中, 客户端只能一次向服务器传输数据, 但是服务器可以多次返回响应
In a single call, the client can only transmit data to the server at one time,
but the server can return the response many times.
```
- #### 双向流模式 (stream-stream)
```text
在一次调用中, 客户端和服务器都可以向对方多次收发数据
In a single call, both client and server can send and receive data 
to each other multiple times.
```

