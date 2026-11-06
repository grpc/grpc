"""
    python server.py
"""

from concurrent import futures
import time

import grpc


def echo(request, context):
    time.sleep(1)
    return request


server = grpc.server(futures.ThreadPoolExecutor(max_workers=8))
server.add_generic_rpc_handlers(
    (
        grpc.method_handlers_generic_handler(
            "forkdebug.Echo", {"Unary": grpc.unary_unary_rpc_method_handler(echo)}
        ),
    )
)
server.add_insecure_port("[::]:50061")
server.start()
print("listening on :50061", flush=True)
server.wait_for_termination()
