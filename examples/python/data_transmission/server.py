# Copyright 2019 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""The example of four ways of data transmission using gRPC in Python."""

from concurrent import futures
from threading import Thread

import grpc

import demo_pb2
import demo_pb2_grpc

__all__ = "DemoServer"
SERVER_ADDRESS = "localhost:23333"
SERVER_ID = 1


class DemoServer(demo_pb2_grpc.GRPCDemoServicer):
    # 一元模式(在一次调用中, 客户端只能向服务器传输一次请求数据, 服务器也只能返回一次响应)
    # unary-unary(In a single call, the client can only send request once, and the server can
    # only respond once.)
    def SimpleMethod(self, request, context):
        print(
            "SimpleMethod called by client(%d) the message: %s"
            % (request.client_id, request.request_data)
        )
        response = demo_pb2.Response(
            server_id=SERVER_ID,
            response_data="Python server SimpleMethod Ok!!!!",
        )
        return response

    # 客户端流模式（在一次调用中, 客户端可以多次向服务器传输数据, 但是服务器只能返回一次响应）
    # stream-unary (In a single call, the client can transfer data to the server several times,
    # but the server can only return a response once.)
    def ClientStreamingMethod(self, request_iterator, context):
        print("ClientStreamingMethod called by client...")
        for request in request_iterator:
            print(
                "recv from client(%d), message= %s"
                % (request.client_id, request.request_data)
            )
        response = demo_pb2.Response(
            server_id=SERVER_ID,
            response_data="Python server ClientStreamingMethod ok",
        )
        return response

    # 服务端流模式（在一次调用中, 客户端只能一次向服务器传输数据, 但是服务器可以多次返回响应）
    # unary-stream (In a single call, the client can only transmit data to the server at one time,
    # but the server can return the response many times.)
    def ServerStreamingMethod(self, request, context):
        print(
            "ServerStreamingMethod called by client(%d), message= %s"
            % (request.client_id, request.request_data)
        )

        # 创建一个生成器
        # create a generator
        def response_messages():
            for i in range(5):
                response = demo_pb2.Response(
                    server_id=SERVER_ID,
                    response_data="send by Python server, message=%d" % i,
                )
                yield response

        return response_messages()

    # 双向流模式 (在一次调用中, 客户端和服务器都可以向对方多次收发数据)
    # stream-stream (In a single call, both client and server can send and receive data
    # to each other multiple times.)
    def BidirectionalStreamingMethod(self, request_iterator, context):
        print("BidirectionalStreamingMethod called by client...")

        # 开启一个子线程去接收数据
        # Open a sub thread to receive data
        def parse_request():
            for request in request_iterator:
                print(
                    "recv from client(%d), message= %s"
                    % (request.client_id, request.request_data)
                )

        t = Thread(target=parse_request)
        t.start()

        for i in range(5):
            yield demo_pb2.Response(
                server_id=SERVER_ID,
                response_data="send by Python server, message= %d" % i,
            )

        t.join()


def main():
    server = grpc.server(futures.ThreadPoolExecutor())

    demo_pb2_grpc.add_GRPCDemoServicer_to_server(DemoServer(), server)

    server.add_insecure_port(SERVER_ADDRESS)
    print("------------------start Python GRPC server")
    server.start()
    server.wait_for_termination()

    # If raise Error:
    #   AttributeError: '_Server' object has no attribute 'wait_for_termination'
    # You can use the following code instead:
    # import time
    # while 1:
    #     time.sleep(10)


if __name__ == "__main__":
    main()
