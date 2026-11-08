"""Server-streaming server for the gRPC Python fork test (see client.py).

    python server.py [DELAY_SECONDS]
"""

from concurrent import futures
import sys
import time

import grpc

DELAY = float(sys.argv[1]) if len(sys.argv) > 1 else 0.05


def server_stream(request, _context):
    # The request is the number of messages to send: b"0", b"1", b"2", ...
    for i in range(int(request)):
        time.sleep(DELAY)  # Keeps the client's receive op pending.
        yield str(i).encode()


server = grpc.server(futures.ThreadPoolExecutor(max_workers=32))
server.add_generic_rpc_handlers(
    (
        grpc.method_handlers_generic_handler(
            "forkdebug.Echo",
            {"ServerStream": grpc.unary_stream_rpc_method_handler(server_stream)},
        ),
    )
)
server.add_insecure_port("[::]:50063")
server.start()
print("listening on :50063", flush=True)
server.wait_for_termination()
