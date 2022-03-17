from concurrent import futures
import time
import threading

import grpc

import pb2_grpc
import pb2

RAISE_ERROR_DELAY_TIME_SENONDS = 1
SEND_REQUEST_AGAIN_DELAY_TIME_SENONDS = 2
HANDLE_REQUEST_DELAY_TIME_SECONDS = 4


class OrderDispatcherServicer(pb2_grpc.TestServiceServicer):
    def testFunc(self, request, context):
        time.sleep(HANDLE_REQUEST_DELAY_TIME_SECONDS)
        return pb2.TestRespnse()


def send_request_to_server(port: int, delay: int):
    channel = grpc.insecure_channel(f"localhost:{port}")
    stub = pb2_grpc.TestServiceStub(channel)
    time.sleep(delay)
    stub.testFunc(
        pb2.TestRequest()
    )

def main():
    server = grpc.server(futures.ThreadPoolExecutor(20))
    pb2_grpc.add_TestServiceServicer_to_server(OrderDispatcherServicer(), server)
    port = server.add_insecure_port("[::]:0")
    server.start()

    threading.Thread(target=send_request_to_server, args=(port, 0)).start()
    threading.Thread(
        target=send_request_to_server,
        args=(port, SEND_REQUEST_AGAIN_DELAY_TIME_SENONDS)
    ).start()
    time.sleep(RAISE_ERROR_DELAY_TIME_SENONDS)
    raise Exception('test')


if __name__ == "__main__":
    main()
