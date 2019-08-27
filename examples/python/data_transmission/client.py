"""
Author: Zhongying Wang
Email: kerbalwzy@gmail.com
DateTime: 2019-08-13T23:30:00Z
PythonVersion: Python3.6.3
"""
import os
import sys
import time
import grpc

# add the `demo_grpc_dps` dir into python package search paths
BaseDir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(BaseDir, "demo_grpc_pbs"))

from demo_grpc_pbs import demo_pb2, demo_pb2_grpc

SERVER_ADDRESS = "localhost:23334"
CLIENT_ID = 1


# 简单模式
# unary-unary
def simple_method(stub):
    print("--------------Call SimpleMethod Begin--------------")
    request = demo_pb2.Request(client_id=CLIENT_ID, request_data="called by Python client")
    response = stub.SimpleMethod(request)
    print("resp from server(%d), the message=%s" % (response.server_id, response.response_data))
    print("--------------Call SimpleMethod Over---------------")


# 客户端流模式（在一次调用中, 客户端可以多次向服务器传输数据, 但是服务器只能返回一次响应）
# stream-unary (In a single call, the client can transfer data to the server several times,
# but the server can only return a response once.)
def client_streaming_method(stub):
    print("--------------Call ClientStreamingMethod Begin--------------")

    # 创建一个生成器
    # create a generator
    def request_messages():
        for i in range(5):
            request = demo_pb2.Request(client_id=CLIENT_ID, request_data=("called by Python client, message:%d" % i))
            yield request

    response = stub.ClientStreamingMethod(request_messages())
    print("resp from server(%d), the message=%s" % (response.server_id, response.response_data))
    print("--------------Call ClientStreamingMethod Over---------------")


# 服务端流模式（在一次调用中, 客户端只能一次向服务器传输数据, 但是服务器可以多次返回响应）
# unary-stream (In a single call, the client can only transmit data to the server at one time,
# but the server can return the response many times.)
def server_streaming_method(stub):
    print("--------------Call ServerStreamingMethod Begin--------------")
    request = demo_pb2.Request(client_id=CLIENT_ID, request_data="called by Python client")
    response_iterator = stub.ServerStreamingMethod(request)
    for response in response_iterator:
        print("recv from server(%d), message=%s" % (response.server_id, response.response_data))

    print("--------------Call ServerStreamingMethod Over---------------")


# 双向流模式 (在一次调用中, 客户端和服务器都可以向对方多次收发数据)
# stream-stream (In a single call, both client and server can send and receive data
# to each other multiple times.)
def bidirectional_streaming_method(stub):
    print("--------------Call BidirectionalStreamingMethod Begin---------------")

    # 创建一个生成器
    # create a generator
    def request_messages():
        for i in range(5):
            request = demo_pb2.Request(client_id=CLIENT_ID, request_data=("called by Python client, message: %d" % i))
            yield request
            time.sleep(1)

    response_iterator = stub.BidirectionalStreamingMethod(request_messages())
    for response in response_iterator:
        print("recv from server(%d), message=%s" % (response.server_id, response.response_data))

    print("--------------Call BidirectionalStreamingMethod Over---------------")


def main():
    with grpc.insecure_channel(SERVER_ADDRESS) as channel:
        stub = demo_pb2_grpc.GRPCDemoStub(channel)

        simple_method(stub)

        client_streaming_method(stub)

        server_streaming_method(stub)

        bidirectional_streaming_method(stub)


if __name__ == '__main__':
    main()
