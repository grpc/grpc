import asyncio
import sys
import time
import grpc

_METHOD = "/test.Benchmark/PingPong"


class GenericHandler(grpc.GenericRpcHandler):
    def __init__(self):
        self.printed = False

    def service(self, handler_call_details):
        if handler_call_details.method == _METHOD:

            async def handler(req, ctx):
                if not self.printed:
                    print(f"Server received type: {type(req)}")
                    if isinstance(req, list):
                        print(f"List length: {len(req)}")
                        if len(req) > 0:
                            print(f"First element type: {type(req[0])}")
                    self.printed = True
                return b"OK"

            return grpc.unary_unary_rpc_method_handler(
                handler,
            )
        return None


async def run_server(port):
    options = [
        ("grpc.max_send_message_length", 160 * 1024 * 1024),
        ("grpc.max_receive_message_length", 160 * 1024 * 1024),
    ]
    server = grpc.aio.server(options=options)
    server.add_generic_rpc_handlers((GenericHandler(),))
    server.add_insecure_port(f"[::]:{port}")
    await server.start()
    return server


async def run_benchmark(port, payload_size, iterations):
    options = [
        ("grpc.max_send_message_length", 160 * 1024 * 1024),
        ("grpc.max_receive_message_length", 160 * 1024 * 1024),
    ]
    channel = grpc.aio.insecure_channel(f"localhost:{port}", options=options)
    try:
        stub = channel.unary_unary(_METHOD)

        payload = b"x" * payload_size

        print(f"Payload size: {payload_size / 1024:.2f} KB")
        print(f"Iterations: {iterations}")

        # Warmup
        for _ in range(5):
            await stub(payload)

        start_time = time.time()
        for _ in range(iterations):
            response = await stub(payload)
            assert response == b"OK", f"Expected b'OK', got {response}"
            if _ == 0:
                print(f"Response type: {type(response)}")

        end_time = time.time()
        total_time = end_time - start_time
        print(f"Total time: {total_time:.4f} seconds")
        print(
            f"Throughput: {payload_size * iterations / (1024*1024) / total_time:.2f} MB/s"
        )
    finally:
        await channel.close()


async def main():
    port = 50052
    server = await run_server(port)
    try:
        await run_benchmark(
            port, payload_size=100 * 1024 * 1024, iterations=100
        )
    finally:
        await server.stop(0)


if __name__ == "__main__":
    asyncio.run(main())
