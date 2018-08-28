Client Configuration Support for GRPC
=====================================

This library provides high level configuration machinery to construct client
channels and load balance between them.

Each grpc_channel is created with a grpc_resolver. It is the resolver's duty
to resolve a name into a set of arguments for the channel. Such arguments
might include:

- a list of (ip, port) addresses to connect to
- a load balancing policy to decide which server to send a request to
- a set of filters to mutate outgoing requests (say, by adding metadata)

The resolver provides this data as a stream of grpc_channel_args objects to
the channel. We represent arguments as a stream so that they can be changed
by the resolver during execution, by reacting to external events (such as
new service configuration data being pushed to some store).


Load Balancing
--------------

Load balancing configuration is provided by a grpc_lb_policy object.

The primary job of the load balancing policies is to pick a target server
given only the initial metadata for a request. It does this by providing
a grpc_subchannel object to the owning channel.


Sub-Channels
------------

A sub-channel provides a connection to a server for a client channel. It has a
connectivity state like a regular channel, and so can be connected or
disconnected. This connectivity state can be used to inform load balancing
decisions (for example, by avoiding disconnected backends).

Configured sub-channels are fully setup to participate in the grpc data plane.
Their behavior is specified by a set of grpc channel filters defined at their
construction. To customize this behavior, resolvers build
grpc_client_channel_factory objects, which use the decorator pattern to customize
construction arguments for concrete grpc_subchannel instances.


Naming for GRPC
===============

See [/doc/naming.md](gRPC name resolution).
