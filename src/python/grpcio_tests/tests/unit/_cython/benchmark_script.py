import asyncio
import multiprocessing
import os
import sys
import time
import grpc

_METHOD = "/test.Benchmark/PingPong"

def run_server(port):
    options = [
        ('grpc.max_send_message_length', 60 * 1024 * 1024),
        ('grpc.max_receive_message_length', 60 * 1024 * 1024),
        ('grpc.so_reuseport', 1),
    ]
    
    async def serve():
        server = grpc.aio.server(options=options)
        
        class GenericHandler(grpc.GenericRpcHandler):
            def service(self, handler_call_details):
                if handler_call_details.method == _METHOD:
                    async def handler(req, ctx):
                        return b'OK'
                    return grpc.unary_unary_rpc_method_handler(
                        handler,
                    )
                return None
                
        server.add_generic_rpc_handlers((GenericHandler(),))
        server.add_insecure_port(f'[::]:{port}')
        await server.start()
        print(f"Server started on pid {os.getpid()}")
        await server.wait_for_termination()
        
    asyncio.run(serve())

def run_client(port, payload_size, iterations, concurrency, result_queue):
    options = [
        ('grpc.max_send_message_length', 60 * 1024 * 1024),
        ('grpc.max_receive_message_length', 60 * 1024 * 1024),
    ]
    
    async def benchmark():
        channel = grpc.aio.insecure_channel(f'localhost:{port}', options=options)
        try:
            stub = channel.unary_unary(_METHOD)
            payload = b'x' * payload_size
            
            # Warmup
            for _ in range(5):
                await stub(payload)
                
            async def worker_func(n):
                for _ in range(n):
                    response = await stub(payload)
                    assert response == b'OK'
                    
            iterations_per_worker = iterations // concurrency
            rem = iterations % concurrency
            
            tasks = []
            for i in range(concurrency):
                n = iterations_per_worker + (1 if i < rem else 0)
                tasks.append(asyncio.create_task(worker_func(n)))
                
            start_time = time.time()
            await asyncio.gather(*tasks)
            end_time = time.time()
            
            return end_time - start_time
        finally:
            await channel.close()
            
    t = asyncio.run(benchmark())
    print(f"Client pid {os.getpid()} finished in {t:.4f} seconds")
    result_queue.put(t)

if __name__ == '__main__':
    cpu_count = os.cpu_count() or 1
    # Use 1/4 of cores for servers, but at least 1
    num_servers = max(1, cpu_count // 4)
    # Use remaining cores for clients, but at least 1
    num_clients = max(1, cpu_count - num_servers)
    
    # Cap at reasonable numbers if needed, but user asked for aggressive
    # Let's use what makes sense for 64 cores.
    
    print(f"System has {cpu_count} cores.")
    print(f"Starting {num_servers} servers and {num_clients} clients.")
    
    port = 50052
    
    servers = []
    for _ in range(num_servers):
        p = multiprocessing.Process(target=run_server, args=(port,))
        p.daemon = True
        p.start()
        servers.append(p)
        
    time.sleep(2) # Wait for servers to bind
    
    payload_size = 10 * 1024 * 1024 # 10MB
    iterations_per_client = 500
    concurrency_per_client = 5
    
    q = multiprocessing.Queue()
    clients = []
    
    print(f"Starting {num_clients} client processes...")
    start_time = time.time()
    for _ in range(num_clients):
        p = multiprocessing.Process(target=run_client, args=(port, payload_size, iterations_per_client, concurrency_per_client, q))
        p.start()
        clients.append(p)
        
    for p in clients:
        p.join()
        
    end_time = time.time()
    total_time = end_time - start_time
    
    total_iterations = iterations_per_client * num_clients
    total_data = payload_size * total_iterations
    
    print(f"All clients finished.")
    print(f"Total time elapsed: {total_time:.4f} seconds")
    print(f"Total data transferred: {total_data / (1024*1024):.2f} MB")
    print(f"Effective Throughput: {total_data / (1024*1024) / total_time:.2f} MB/s")
    
    # Collect individual times (optional)
    times = []
    while not q.empty():
        times.append(q.get())
    if times:
        print(f"Average client time: {sum(times)/len(times):.4f} seconds")
        
    # Terminate servers
    print("Terminating servers...")
    for p in servers:
        p.terminate()
        p.join()
