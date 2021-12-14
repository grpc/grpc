# Retry Example in gRPC Python

## Prerequisite

* grpcio >= 1.39.0
* grpcio-tools >= 1.39.0

## Running the example

In terminal 1, start the flaky server:

```sh
python3 flaky_server.py
```

In terminal 2, start the retry clients:

```sh
python3 retry_client.py
# Or
python3 async_retry_client.py
```

## Expect results

The client RPC will succeed, even with server injecting multiple errors. Here is an example server log:

```sh
$ python3 flaky_server.py
INFO:root:Starting flaky server on [::]:50051
INFO:root:Injecting error to RPC from ipv6:[::1]:54471
INFO:root:Successfully responding to RPC from ipv6:[::1]:54473
INFO:root:Injecting error to RPC from ipv6:[::1]:54491
INFO:root:Injecting error to RPC from ipv6:[::1]:54581
INFO:root:Injecting error to RPC from ipv6:[::1]:54581
INFO:root:Injecting error to RPC from ipv6:[::1]:54581
INFO:root:Injecting error to RPC from ipv6:[::1]:54581
INFO:root:Successfully responding to RPC from ipv6:[::1]:54581
INFO:root:Injecting error to RPC from ipv6:[::1]:55474
INFO:root:Injecting error to RPC from ipv6:[::1]:55474
INFO:root:Injecting error to RPC from ipv6:[::1]:55474
INFO:root:Injecting error to RPC from ipv6:[::1]:55474
INFO:root:Successfully responding to RPC from ipv6:[::1]:55474
INFO:root:Injecting error to RPC from ipv6:[::1]:55533
INFO:root:Injecting error to RPC from ipv6:[::1]:55533
INFO:root:Injecting error to RPC from ipv6:[::1]:55533
INFO:root:Successfully responding to RPC from ipv6:[::1]:55533
```
