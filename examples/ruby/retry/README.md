# Retry Example in gRPC Ruby

## Prerequisite

* grpcio >= 1.39.0
* grpcio-tools >= 1.39.0

## Running the example

In terminal 1, start the flaky server:

```sh
ruby flaky_server.rb
```

In terminal 2, start the retry client:

```sh
ruby retry_client.rb
```

## Expect results

The client RPC will succeed, even with server injecting multiple errors. Here is an example server log:

```sh
$ ruby flaky_server.rb
INFO: Injecting error to RPC from ipv4:127.0.0.1:60076
INFO: Injecting error to RPC from ipv4:127.0.0.1:60076
INFO: Injecting error to RPC from ipv4:127.0.0.1:60076
INFO: Injecting error to RPC from ipv4:127.0.0.1:60076
INFO: Successfully responding to RPC from ipv4:127.0.0.1:60076
INFO: Injecting error to RPC from ipv4:127.0.0.1:60078
INFO: Successfully responding to RPC from ipv4:127.0.0.1:60078
```
