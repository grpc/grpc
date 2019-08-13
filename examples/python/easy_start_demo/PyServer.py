"""
Author: Zhongying Wang
Email: kerbalwzy@gmail.com
License: MPL2
DateTime: 2019-08-13T23:30:00Z
PythonVersion: Python3.6.3
"""
import time

import grpc
from threading import Thread
from concurrent import futures
from customGrpcPackages import demo_pb2, demo_pb2_grpc

# Constants
ServerAddress = '127.0.0.1:23334'
ServerId = 1


class DemoServer(demo_pb2_grpc.GRPCDemoServicer):

    # 简单模式
    # Unary
    def SimpleMethod(self, request, context):
        print(f"SimpleMethod called by client({request.Cid}) the message: {request.ReqMsg}")
        resp = demo_pb2.Response(Sid=ServerId, RespMsg="Python server SimpleMethod Ok!!!!")
        return resp

    # 客户端流模式（在一次调用中, 客户端可以多次向服务器传输数据, 但是服务器只能返回一次响应）
    # Client Streaming (In a single call, the client can transfer data to the server several times,
    # but the server can only return a response once.)
    def CStreamMethod(self, request_iterator, context):
        print("CStreamMethod called by client...")
        for req in request_iterator:
            print(f"recv from client({req.Cid}), message={req.ReqMsg}")
        resp = demo_pb2.Response(Sid=ServerId, RespMsg="Python server CStreamMethod ok")
        return resp

    # 服务端流模式（在一次调用中, 客户端只能一次向服务器传输数据, 但是服务器可以多次返回响应）
    # Server Streaming (In a single call, the client can only transmit data to the server at one time,
    # but the server can return the response many times.)
    def SStreamMethod(self, request, context):
        print(f"SStreamMethod called by client({request.Cid}), message={request.ReqMsg}")

        # 创建一个生成器
        def resp_msgs():
            for i in range(5):
                resp = demo_pb2.Response(Sid=ServerId, RespMsg=f"send by Python server, message={i}")
                yield resp

        return resp_msgs()

    # 双向流模式 (在一次调用中, 客户端和服务器都可以向对方多次收发数据)
    # Bidirectional Streaming (In a single call, both client and server can send and receive data
    # to each other multiple times.)
    def TWFMethod(self, request_iterator, context):
        # 开启一个子线程去接收数据
        # Open a sub thread to receive data
        def parse_req():
            for req in request_iterator:
                print(f"recv from client{req.Cid}, message={req.ReqMsg}")

        t = Thread(target=parse_req)
        t.start()

        for i in range(5):
            yield demo_pb2.Response(Sid=ServerId, RespMsg=f"send by Python server, message={i}")

        t.join()


def main():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))

    demo_pb2_grpc.add_GRPCDemoServicer_to_server(DemoServer(), server)

    server.add_insecure_port(ServerAddress)
    print("------------------start Python GRPC server")
    server.start()

    # In python3, `server` have no attribute `wait_for_termination`
    while 1:
        time.sleep(10)


if __name__ == '__main__':
    main()
