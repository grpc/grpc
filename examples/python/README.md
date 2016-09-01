gRPC in 3 minutes (Python)
========================

Background
-------------
For this sample, we've already generated the server and client stubs from
[helloworld.proto][] and we'll be using a specific reference platform.


Install gRPC:
```sh
  $ pip install grpcio
```
Or, to install it system wide:
```sh
  $ sudo pip install grpcio
```

If you're on Windows, make sure you installed the `pip.exe` component when you
installed Python. Invoke as above but with `pip.exe` instead of `pip` (you may
also need to invoke from a `cmd.exe` ran as administrator):
```sh
  $ pip.exe install grpcio
```

Download the example
```sh
  $ # Clone the repository to get the example code:
  $ git clone https://github.com/grpc/grpc
  $ # Navigate to the "hello, world" Python example:
  $ cd grpc/examples/python/helloworld
  ```

Try it!
-------

- Run the server

  ```sh
  $ python2.7 greeter_server.py &
  ```

- Run the client

  ```sh
  $ python2.7 greeter_client.py
  ```

Tutorial
--------

You can find a more detailed tutorial in [gRPC Basics: Python][]

[helloworld.proto]:../protos/helloworld.proto
[Install gRPC Python]:../../src/python#installation
[gRPC Basics: Python]:http://www.grpc.io/docs/tutorials/basic/python.html
