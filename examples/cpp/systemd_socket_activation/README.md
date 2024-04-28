gRPC systemd Socket Activation Example
================

This example shows how to use systemd's [socket-based activation](https://www.freedesktop.org/software/systemd/man/sd_listen_fds_with_names.html) with gRPC.

### Build and run the example

The simplest way to exercise this is via `test.sh`. It roughly does the following:

* Creates a systemd socket-activated service.
* Builds the gRPC client & server.
* Reloads the systemd daemon.
* Runs the gRPC client, which in turn runs the gRPC server.

The test script runs multiple tests in sequence, order to :

- show how to define the parameters for systemd
- show what to use as grpc server endpoint
- validate that these tuples of settings  and validate that systemd socket activation work

The client log is printed on the console when run.

The server log can be viewed in Systemd journal.

### What cannot be implemented

- Wildcard ports (`:0`) to auto-choose a port, as systemd does not offer the possibility of dynamic ports.

### What is implemented and tested

For the specific tested cases and expected results, see `test.sh`.

General guidelines are the following :

- Unix standard socket with absolute path :
  - Systemd : `ListenStream=/tmp/server`
  - gRPC server : `unix:/tmp/server` or `unix:///tmp/server`
    (please mind the triple-slash, as stated in `Naming.md`)
- Unix standard socket with relative path :
  - Warning: *not compatible with symlinks in the socket path !*
  - Systemd : `ListenStream=/tmp/server` and `WorkingDirectory=/tmp`
  - gRPC server : `unix:server` or `unix:./server`
- Unix abstract socket by name :
  - Systemd : `ListenStream=@test_unix_abstract`
  - gRPC server : `unix-abstract:test_unix_abstract`
- IPv4 addresses :
  - Systemd : `ListenStream=127.0.0.1:3456`
  - gRPC server : `127.0.0.1:3456`
- IPv4 wildcard addresses :
  - Systemd : `ListenStream=0.0.0.0:3456`
  - gRPC server : `0.0.0.0:3456`
- IPv6 addresses :
  - in dual-stack mode, use `BindIPv6Only=both` (or leave `BindIPv6Only=default`) in systemd socket unit
  - in ipv6-only mode, use `BindIPv6Only=ipv6-only` in systemd socket unit
- IPv6 addresses :
  - Systemd : `ListenStream=[::1]:3456`
  - gRPC server : `[::1]:3456`
- IPv6 wildcard addresses :
  - Systemd : `ListenStream=[::]:3456`
  - gRPC server : `[::]:3456`
- DNS records, resolving to addresses *in one of the formats listed above* :
  - Warning : Systemd does not allow using DNS records in socket units
  - Notice : on the server side, `example.org` resolves to `192.168.1.1`
  - Systemd : `ListenStream=192.168.1.1:3456`
  - gRPC server : `example.org:3456`
- Option `grpc.expand_wildcard_addrs`, as weird as it would be :
  - Systemd : one or multiple existing interface, such as `ListenStream=[::1]:3456`
  - gRPC server : `[::]:3456`, which would expand into "one per host interface",
    some of which would actually match the ones configured in Systemd
    (for example, the `[::1]` above).
  - So please mind the fact that Systemd will only start the server
    *for connection coming on the addresses it is configured to listen on*.
    So even though gRPC wildcard expansion will create additional listening
    sockets to listen on, these listeners are only known to gRPC, and any
    incoming connections on these listener will of course *not* trigger
    an activation through Systemd. As a consequence, these additional gRPC
    listeners would only be reachable when the server is running. If you
    want more interface listeners to trigger activation, use additional
    `ListenStream=` stanzas in your Systemd socket unit.

### What is not implemented

- Vsock addresses
- Interface scope `[x]:y%dev`, for IPv6 link-local addresses
