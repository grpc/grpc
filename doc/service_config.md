Service Config in gRPC
======================

# Objective

The service config is a mechanism that allows service owners to publish
parameters to be automatically used by all clients of their service.

# Format

The service config is a JSON string of the following form:

```
{
  # Load balancing policy name.
  # Supported values are 'round_robin' and 'grpclb'.
  # Optional; if unset, the default behavior is pick the first available
  # backend.
  # Note that if the resolver returns only balancer addresses and no
  # backend addresses, gRPC will always use the 'grpclb' policy,
  # regardless of what this field is set to.
  'loadBalancingPolicy': string,

  # Per-method configuration.  Optional.
  'methodConfig': [
    {
      # The names of the methods to which this method config applies. There
      # must be at least one name. Each name entry must be unique across the
      # entire service config. If the 'method' field is empty, then this
      # method config specifies the defaults for all methods for the specified
      # service.
      #
      # For example, let's say that the service config contains the following
      # method config entries:
      #
      # 'methodConfig': [
      #   { 'name': [ { 'service': 'MyService' } ] ... },
      #   { 'name': [ { 'service': 'MyService', 'method': 'Foo' } ] ... }
      # ]
      #
      # For a request for MyService/Foo, we will use the second entry, because
      # it exactly matches the service and method name.
      # For a request for MyService/Bar, we will use the first entry, because
      # it provides the default for all methods of MyService.
      'name': [
        {
          # RPC service name.  Required.
          # If using gRPC with protobuf as the IDL, then this will be of
          # the form "pkg.service_name", where "pkg" is the package name
          # defined in the proto file.
          'service': string,

          # RPC method name.  Optional (see above).
          'method': string,
        }
      ],

      # Whether RPCs sent to this method should wait until the connection is
      # ready by default. If false, the RPC will abort immediately if there
      # is a transient failure connecting to the server. Otherwise, gRPC will
      # attempt to connect until the deadline is exceeded.
      #
      # The value specified via the gRPC client API will override the value
      # set here. However, note that setting the value in the client API will
      # also affect transient errors encountered during name resolution,
      # which cannot be caught by the value here, since the service config
      # is obtained by the gRPC client via name resolution.
      'waitForReady': bool,

      # The default timeout in seconds for RPCs sent to this method. This can
      # be overridden in code. If no reply is received in the specified amount
      # of time, the request is aborted and a deadline-exceeded error status
      # is returned to the caller.
      #
      # The actual deadline used will be the minimum of the value specified
      # here and the value set by the application via the gRPC client API.
      # If either one is not set, then the other will be used.
      # If neither is set, then the request has no deadline.
      #
      # The format of the value is that of the 'Duration' type defined here:
      # https://developers.google.com/protocol-buffers/docs/proto3#json
      'timeout': string,

      # The maximum allowed payload size for an individual request or object
      # in a stream (client->server) in bytes. The size which is measured is
      # the serialized, uncompressed payload in bytes. This applies both
      # to streaming and non-streaming requests.
      #
      # The actual value used is the minimum of the value specified here and
      # the value set by the application via the gRPC client API.
      # If either one is not set, then the other will be used.
      # If neither is set, then the built-in default is used.
      #
      # If a client attempts to send an object larger than this value, it
      # will not be sent and the client will see an error.
      # Note that 0 is a valid value, meaning that the request message must
      # be empty.
      #
      # The format of the value is that of the 'uint64' type defined here:
      # https://developers.google.com/protocol-buffers/docs/proto3#json
      'maxRequestMessageBytes': string,

      # The maximum allowed payload size for an individual response or object
      # in a stream (server->client) in bytes. The size which is measured is
      # the serialized, uncompressed payload in bytes. This applies both
      # to streaming and non-streaming requests.
      #
      # The actual value used is the minimum of the value specified here and
      # the value set by the application via the gRPC client API.
      # If either one is not set, then the other will be used.
      # If neither is set, then the built-in default is used.
      #
      # If a server attempts to send an object larger than this value, it
      # will not be sent, and the client will see an error.
      # Note that 0 is a valid value, meaning that the response message must
      # be empty.
      #
      # The format of the value is that of the 'uint64' type defined here:
      # https://developers.google.com/protocol-buffers/docs/proto3#json
      'maxResponseMessageBytes': string
    }
  ]
}
```

# Architecture

A service config is associated with a server name.  The [name
resolver](naming.md) plugin, when asked to resolve a particular server
name, will return both the resolved addresses and the service config.

TODO(roth): Design how the service config will be encoded in DNS.




The gRPC load balancing implements the external load balancing server approach:
an external load balancer provides simple clients with an up-to-date list of
servers.

![image](images/load_balancing_design.png)

1. On startup, the gRPC client issues a name resolution request for the service.
   The name will resolve to one or more IP addresses to gRPC servers, a hint on
   whether the IP address(es) point to a load balancer or not, and also return a
   client config.
2. The gRPC client connects to a gRPC Server.
   1. If the name resolution has hinted that the endpoint is a load balancer,
      the client's gRPC LB policy will attempt to open a stream to the load
      balancer service. The server may respond in only one of the following
      ways.
      1. `status::UNIMPLEMENTED`. There is no loadbalancing in use. The client
         call will fail.
      2. "I am a Load Balancer and here is the server list." (Goto Step 4.)
      3. "Please contact Load Balancer X" (See Step 3.) The client will close
         this connection and cancel the stream.
      4. If the server fails to respond, the client will wait for some timeout
         and then re-resolve the name (process to Step 1 above).
   2. If the name resolution has not hinted that the endpoint is a load
      balancer, the client connects directly to the service it wants to talk to.
3. The gRPC client's gRPC LB policy opens a separate connection to the Load
   Balancer. If this fails, it will go back to step 1 and try another address.
   1. During channel initialization to the Load Balancer, the client will
      attempt to open a stream to the Load Balancer service.
   2. The Load Balancer will return a server list to the gRPC client. If the
      server list is empty, the call will wait until a non-empty one is
      received. Optional: The Load Balancer will also open channels to the gRPC
      servers if load reporting is needed.
4. The gRPC client will send RPCs to the gRPC servers contained in the server
   list from the Load Balancer.
5. Optional: The gRPC servers may periodically report load to the Load Balancer.

## Client

When establishing a gRPC _stream_ to the balancer, the client will send an initial
request to the load balancer (via a regular gRPC message). The load balancer
will respond with client config (including, for example, settings for flow
control, RPC deadlines, etc.) or a redirect to another load balancer. If the
balancer did not redirect the client, it will then send a list of servers to the
client. The client will contain simple load balancing logic for choosing the
next server when it needs to send a request.

## Load Balancer

The Load Balancer is responsible for providing the client with a list of servers
and client RPC parameters. The balancer chooses when to update the list of
servers and can decide whether to provide a complete list, a subset, or a
specific list of “picked” servers in a particular order. The balancer can
optionally provide an expiration interval after which the server list should no
longer be trusted and should be updated by the balancer.

The load balancer may open reporting streams to each server contained in the
server list. These streams are primarily used for load reporting. For example,
Weighted Round Robin requires that the servers report utilization to the load
balancer in order to compute the next list of servers.

## Server

The gRPC Server is responsible for answering RPC requests and providing
responses to the client. The server will also report load to the load balancer
if a reporting stream was opened for this purpose.
