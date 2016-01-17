gRPC in 3 minutes (Python)
========================

Background
-------------
For this sample, we've already generated the server and client stubs from
[helloworld.proto][] and we'll be using a specific reference platform.

Prerequisites
-------------

- Debian 8.2 "Jessie" platform with `root` access
- `git`
- `python2.7`
- `pip`
- Python development headers

Set-up
-------
  ```sh
  $ # install the gRPC Core:
  $ sudo apt-get install libgrpc-dev
  $ # install gRPC Python:
  $ sudo pip install -U grpcio==0.11.0b1
  $ # Since this "hello, world" example uses protocol buffers:
  $ sudo pip install -U protobuf==3.0.0a3
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
