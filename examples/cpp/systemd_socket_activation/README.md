gRPC systemd Socket Activation Example
================

This example shows how to use systemd's [socket-based activation](https://www.freedesktop.org/software/systemd/man/sd_listen_fds_with_names.html) with gRPC.

### Build and run the example

The simplest way to exercise this is via `test.sh`. It roughly does the following:

* Creates a systemd socket-activated service.
* Builds the gRPC client & server.
* Reloads the systemd daemon.
* Runs the gRPC client, which in turn runs the gRPC server.
