GRPC Health Checking Protocol
================================

Health checks are used to probe whether the server is able to handle rpcs. The
client-to-server health checking can happen from point to point or via some
load balancing mechanism. A server may choose to reply “unhealthy” because it
has not been ready to take requests, it is shutting down or some other reason.
The client can act accordingly if the response is not received within some time
window or the response says unhealthy in it.


A GRPC service is used as the health checking mechanism for both simple
client-to-server scenario and other systems such as load-balancing. Being a high
level service provides some benefits. Firstly, since it is a GRPC service
itself, doing a health check is in the same format as a normal rpc. Secondly,
it has rich semantics such as per-service health status. Thirdly, as a GRPC
service, it is able reuse all the existing billing, quota infrastructure, etc,
and thus the server has full control over the access of the health checking
service.

## Service Definition

The server should export a service defined in the following proto:

```
syntax = "proto3";

package grpc.health.v1alpha;

message HealthCheckRequest {
  string host = 1;
  string service = 2;
}

message HealthCheckResponse {
  enum ServingStatus {
    UNKNOWN = 0;
    SERVING = 1;
    NOT_SERVING = 2;
  }
  ServingStatus status = 1;
}

service Health {
  rpc Check(HealthCheckRequest) returns (HealthCheckResponse);
}
```

A client can query the server’s health status by calling the `Check` method, and
a deadline should be set on the rpc. The client can optionally set the host
string. The client can optionally set the service name it wants to query for
health status. The suggested format of service name is
`grpc.health.v1alpha.Health`.

The server should register all the services for all the hosts manually and set
the individual status, including an empty service name and its status. For each
request received, if the (host, service_name) pair can be found in the registry,
a response must be sent back with an `OK` status and the status field should be
set to `SERVING` or `NOT_SERVING` accordingly. If the service name is not
registered, the server returns a `NOT_FOUND` GRPC status.

It is recommended that the server use an empty string as the default host string
and create a (“”, “”) pair as the key for server’s health status as a whole. The
server can just do exact matching of the (host, service_name) pair and does not
support any kind of wildcard matching. However, the service owner has the
freedom to implement more complicated matching semantics that both the client
and server agree upon.


A client can declare the server as unhealthy if the rpc is not finished after
some amount of time. The client should be able to handle the case where server
does not have the Health service.
