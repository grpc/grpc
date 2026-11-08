"""Client-streaming server for the gRPC Python fork test (see client.py).

    python server.py [DELAY_SECONDS]
"""

from concurrent import futures
import sys
import time

import grpc

DELAY = float(sys.argv[1]) if len(sys.argv) > 1 else 0.05


def client_stream(request_iterator, _context):
    count = 0
    for _ in request_iterator:
        time.sleep(DELAY)  # Keeps the call in flight on the client.
        count += 1
    return str(count).encode()


server = grpc.server(futures.ThreadPoolExecutor(max_workers=32))
server.add_generic_rpc_handlers(
    (
        grpc.method_handlers_generic_handler(
            "forkdebug.Echo",
            {"ClientStream": grpc.stream_unary_rpc_method_handler(client_stream)},
        ),
    )
)
server.add_insecure_port("[::]:50064")
server.start()
print("listening on :50064", flush=True)
server.wait_for_termination()
