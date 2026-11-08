"""Bidi-streaming echo server for the gRPC Python fork test (see client.py).

    python server.py [DELAY_SECONDS]
"""

from concurrent import futures
import sys
import time

import grpc

DELAY = float(sys.argv[1]) if len(sys.argv) > 1 else 0.05


def bidi_echo(request_iterator, _context):
    for request in request_iterator:
        time.sleep(DELAY)  # Keeps the client's receive op pending.
        yield request


server = grpc.server(futures.ThreadPoolExecutor(max_workers=32))
server.add_generic_rpc_handlers(
    (
        grpc.method_handlers_generic_handler(
            "forkdebug.Echo",
            {"Bidi": grpc.stream_stream_rpc_method_handler(bidi_echo)},
        ),
    )
)
server.add_insecure_port("[::]:50062")
server.start()
print("listening on :50062", flush=True)
server.wait_for_termination()
