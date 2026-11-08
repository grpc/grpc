"""Server for the gRPC Python fork test with all four RPC types (see client.py).

    python server.py [DELAY_SECONDS]

Every handler sleeps DELAY per message, so the client's calls are still in
flight when it forks.
"""

from concurrent import futures
import sys
import time

import grpc

DELAY = float(sys.argv[1]) if len(sys.argv) > 1 else 0.05


def unary(request, _context):
    time.sleep(DELAY)
    return request


def server_stream(request, _context):
    # The request is the number of messages to send: b"0", b"1", b"2", ...
    for i in range(int(request)):
        time.sleep(DELAY)
        yield str(i).encode()


def client_stream(request_iterator, _context):
    count = 0
    for _ in request_iterator:
        time.sleep(DELAY)
        count += 1
    return str(count).encode()


def bidi(request_iterator, _context):
    for request in request_iterator:
        time.sleep(DELAY)
        yield request


server = grpc.server(futures.ThreadPoolExecutor(max_workers=64))
server.add_generic_rpc_handlers(
    (
        grpc.method_handlers_generic_handler(
            "forkdebug.Echo",
            {
                "Unary": grpc.unary_unary_rpc_method_handler(unary),
                "ServerStream": grpc.unary_stream_rpc_method_handler(server_stream),
                "ClientStream": grpc.stream_unary_rpc_method_handler(client_stream),
                "Bidi": grpc.stream_stream_rpc_method_handler(bidi),
            },
        ),
    )
)
server.add_insecure_port("[::]:50065")
server.start()
print("listening on :50065", flush=True)
server.wait_for_termination()
