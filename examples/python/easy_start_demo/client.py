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

ServerAddress = "127.0.0.1:23334"
ClientId = 1


# 简单模式
# Simple Method
def simple_method(stub):
    print("--------------Call SimpleMethod Begin--------------")
    req = demo_pb2.Request(Cid=ClientId, ReqMsg="called by Python client")
    resp = stub.SimpleMethod(req)
    print("resp from server(%d), the message=%s" % (resp.Sid, resp.RespMsg))
    print("--------------Call SimpleMethod Over---------------")


# 客户端流模式（在一次调用中, 客户端可以多次向服务器传输数据, 但是服务器只能返回一次响应）
# Request-streaming (In a single call, the client can transfer data to the server several times,
# but the server can only return a response once.)
def client_streaming_method(stub):
    print("--------------Call ClientStreamingMethod Begin--------------")

    # 创建一个生成器
    # create a generator
    def request_messages():
        for i in range(5):
            req = demo_pb2.Request(Cid=ClientId, ReqMsg=("called by Python client, message:%d" % i))
            yield req

    resp = stub.ClientStreamingMethod(request_messages())
    print("resp from server(%d), the message=%s" % (resp.Sid, resp.RespMsg))
    print("--------------Call ClientStreamingMethod Over---------------")


# 服务端流模式（在一次调用中, 客户端只能一次向服务器传输数据, 但是服务器可以多次返回响应）
# Response-streaming (In a single call, the client can only transmit data to the server at one time,
# but the server can return the response many times.)
def server_streaming_method(stub):
    print("--------------Call ServerStreamingMethod Begin--------------")
    req = demo_pb2.Request(Cid=ClientId, ReqMsg="called by Python client")
    resp_s = stub.ServerStreamingMethod(req)
    for resp in resp_s:
        print("recv from server(%d), message=%s" % (resp.Sid, resp.RespMsg))

    print("--------------Call ServerStreamingMethod Over---------------")


# 双向流模式 (在一次调用中, 客户端和服务器都可以向对方多次收发数据)
# Bidirectional Streaming (In a single call, both client and server can send and receive data
# to each other multiple times.)
def bidirectional_streaming_method(stub):
    print("--------------Call BidirectionalStreamingMethod Begin---------------")

    # 创建一个生成器
    # create a generator
    def req_messages():
        for i in range(5):
            req = demo_pb2.Request(Cid=ClientId, ReqMsg=("called by Python client, message: %d" % i))
            yield req
            time.sleep(1)

    resp_s = stub.BidirectionalStreamingMethod(req_messages())
    for resp in resp_s:
        print("recv from server(%d), message=%s" % (resp.Sid, resp.RespMsg))

    print("--------------Call BidirectionalStreamingMethod Over---------------")


def main():
    with grpc.insecure_channel(ServerAddress) as channel:
        stub = demo_pb2_grpc.GRPCDemoStub(channel)

        simple_method(stub)

        client_streaming_method(stub)

        server_streaming_method(stub)

        bidirectional_streaming_method(stub)


if __name__ == '__main__':
    main()
