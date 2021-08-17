## Multiprocessing with gRPC Python

Multiprocessing allows application developers to sidestep the Python global
interpreter lock and achieve true parallelism on multicore systems.
Unfortunately, using multiprocessing and gRPC Python is not yet as simple as
instantiating your server with a `futures.ProcessPoolExecutor`.

The library is implemented as a C extension, maintaining much of the state that
drives the system in native code. As such, upon calling
[`fork`](http://man7.org/linux/man-pages/man2/fork.2.html), any threads in a
critical section may leave the state of the gRPC library invalid in the child
process. See this [excellent research
paper](https://www.microsoft.com/en-us/research/uploads/prod/2019/04/fork-hotos19.pdf)
for a thorough discussion of the topic.

Calling `fork` without `exec` in your process *is* supported
before any gRPC servers have been instantiated. Application developers can
take advantage of this to parallelize their CPU-intensive operations.

## Calculating Prime Numbers with Multiple Processes

This example calculates the first 10,000 prime numbers as an RPC. We instantiate
one server per subprocess, balancing requests between the servers using the
[`SO_REUSEPORT`](https://lwn.net/Articles/542629/) socket option.

```python
_PROCESS_COUNT = multiprocessing.cpu_count()
```

On the server side, we detect the number of CPUs available on the system and
spawn exactly that many child processes. If we spin up fewer, we won't be taking
full advantage of the hardware resources available.

## Running the Example

To run the server,
[ensure `bazel` is installed](https://docs.bazel.build/versions/master/install.html)
and run:

```
bazel run //examples/python/multiprocessing:server &
```

Note the address at which the server is running. For example,

```
...
[PID 107153] Binding to '[::]:33915'
[PID 107507] Starting new server.
[PID 107508] Starting new server.
...
```

Note that several servers have been started, each with its own PID.

Now, start the client by running

```
bazel run //examples/python/multiprocessing:client -- [SERVER_ADDRESS]
```

For example,

```
bazel run //examples/python/multiprocessing:client -- [::]:33915
```

Alternatively, generate code using the following and then run the client and server
directly:

```python
cd examples/python/helloworld
python -m grpc_tools.protoc -I . prime.proto  --python_out=. --grpc_python_out=.
```
