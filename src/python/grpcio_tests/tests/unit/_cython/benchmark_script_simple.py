import sys
import time
from concurrent import futures
import grpc

_METHOD = "/test.Benchmark/PingPong"

def run_server(port):
    options = [
        ('grpc.max_send_message_length', 60 * 1024 * 1024),
        ('grpc.max_receive_message_length', 60 * 1024 * 1024),
    ]
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1), options=options)
    class GenericHandler(grpc.GenericRpcHandler):
        def __init__(self):
            self.printed = False
            
        def service(self, handler_call_details):
            if handler_call_details.method == _METHOD:
                def handler(req, ctx):
                    if not self.printed:
                        print(f"Server received type: {type(req)}")
                        if isinstance(req, list):
                            print(f"List length: {len(req)}")
                            if len(req) > 0:
                                print(f"First element type: {type(req[0])}")
                        self.printed = True
                    return b'OK'
                return grpc.unary_unary_rpc_method_handler(
                    handler,
                )
            return None
    server.add_generic_rpc_handlers((GenericHandler(),))
    server.add_insecure_port(f'[::]:{port}')
    server.start()
    return server

def run_benchmark(port, payload_size, iterations):
    options = [
        ('grpc.max_send_message_length', 60 * 1024 * 1024),
        ('grpc.max_receive_message_length', 60 * 1024 * 1024),
    ]
    channel = grpc.insecure_channel(f'localhost:{port}', options=options)
    stub = channel.unary_unary(
        _METHOD,
    )
    
    payload = b'x' * payload_size
    
    print(f"Payload size: {payload_size / 1024:.2f} KB")
    print(f"Iterations: {iterations}")
    
    # Warmup
    for _ in range(5):
        stub(payload)
        
    start_time = time.time()
    for _ in range(iterations):
        response = stub(payload)
        assert response == b'OK', f"Expected b'OK', got {response}"
        if _ == 0:
             print(f"Response type: {type(response)}")
             
    end_time = time.time()
    total_time = end_time - start_time
    print(f"Total time: {total_time:.4f} seconds")
    print(f"Throughput: {payload_size * iterations / (1024*1024) / total_time:.2f} MB/s")
    
    channel.close()

if __name__ == '__main__':
    port = 50052  # Use a different port to avoid conflicts
    server = run_server(port)
    try:
        run_benchmark(port, payload_size=10 * 1024 * 1024, iterations=5000)
    finally:
        server.stop(0)
