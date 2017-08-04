Load Balancing in gRPC
======================

# Scope

This document explains the design for load balancing within gRPC.

# Background

## Per-Call Load Balancing

It is worth noting that load-balancing within gRPC happens on a per-call
basis, not a per-connection basis.  In other words, even if all requests
come from a single client, we still want them to be load-balanced across
all servers.

## Approaches to Load Balancing

Prior to any gRPC specifics, we explore some usual ways to approach load
balancing.

### Proxy Model

Using a proxy provides a solid trustable client that can report load to the load
balancing system. Proxies typically require more resources to operate since they
have temporary copies of the RPC request and response. This model also increases
latency to the RPCs.

The proxy model was deemed inefficient when considering request heavy services
like storage.

### Balancing-aware Client

This thicker client places more of the load balancing logic in the client. For
example, the client could contain many load balancing policies (Round Robin,
Random, etc) used to select servers from a list. In this model, a list of
servers would be either statically configured in the client, provided by the
name resolution system, an external load balancer, etc. In any case, the client
is responsible for choosing the preferred server from the list.

One of the drawbacks of this approach is writing and maintaining the load
balancing policies in multiple languages and/or versions of the clients. These
policies can be fairly complicated. Some of the algorithms also require client
to server communication so the client would need to get thicker to support
additional RPCs to get health or load information in addition to sending RPCs
for user requests.

It would also significantly complicate the client's code: the new design hides
the load balancing complexity of multiple layers and presents it as a simple
list of servers to the client.

### External Load Balancing Service

The client load balancing code is kept simple and portable, implementing
well-known algorithms (e.g., Round Robin) for server selection.
Complex load balancing algorithms are instead provided by the load
balancer. The client relies on the load balancer to provide _load
balancing configuration_ and _the list of servers_ to which the client
should send requests. The balancer updates the server list as needed
to balance the load as well as handle server unavailability or health
issues. The load balancer will make any necessary complex decisions and
inform the client. The load balancer may communicate with the backend
servers to collect load and health information.

# Requirements

## Simple API and client

The gRPC client load balancing code must be simple and portable. The
client should only contain simple algorithms (e.g., Round Robin) for
server selection.  For complex algorithms, the client should rely on
a load balancer to provide load balancing configuration and the list of
servers to which the client should send requests. The balancer will update
the server list as needed to balance the load as well as handle server
unavailability or health issues. The load balancer will make any necessary
complex decisions and inform the client. The load balancer may communicate
with the backend servers to collect load and health information.

## Security

The load balancer may be separate from the actual server backends and a
compromise of the load balancer should only lead to a compromise of the
loadbalancing functionality. In other words, a compromised load balancer should
not be able to cause a client to trust a (potentially malicious) backend server
any more than in a comparable situation without loadbalancing.

# Architecture

## Overview

The primary mechanism for load-balancing in gRPC is external
load-balancing, where an external load balancer provides simple clients
with an up-to-date list of servers.

The gRPC client does support an API for built-in load balancing policies.
However, there are only a small number of these (one of which is the
`grpclb` policy, which implements external load balancing), and users
are discouraged from trying to extend gRPC by adding more.  Instead, new
load balancing policies should be implemented in external load balancers.

## Workflow

Load-balancing policies fit into the gRPC client workflow in between
name resolution and the connection to the server.  Here's how it all
works:

![image](images/load-balancing.png)

1. On startup, the gRPC client issues a [name resolution](naming.md) request
   for the server name.  The name will resolve to one or more IP addresses,
   each of which will indicate whether it is a server address or
   a load balancer address, and a [service config](service_config.md)
   that indicates which client-side load-balancing policy to use (e.g.,
   `round_robin` or `grpclb`).
2. The client instantiates the load balancing policy.
   - Note: If all addresses returned by the resolver are balancer
     addresses, then the client will use the `grpclb` policy, regardless
     of what load-balancing policy was requested by the service config.
     Otherwise, the client will use the load-balancing policy requested
     by the service config.  If no load-balancing policy is requested
     by the service config, then the client will default to a policy
     that picks the first available server address.
3. The load balancing policy creates a subchannel to each server address.
   - For all policies *except* `grpclb`, this means one subchannel for each
     address returned by the resolver. Note that these policies
     ignore any balancer addresses returned by the resolver.
   - In the case of the `grpclb` policy, the workflow is as follows:
     1. The policy opens a stream to one of the balancer addresses returned
        by the resolver. It asks the balancer for the server addresses to
        use for the server name originally requested by the client (i.e.,
        the same one originally passed to the name resolver).
        - Note: The `grpclb` policy currently ignores any non-balancer
          addresses returned by the resolver. However, in the future, it
          may be changed to use these addresses as a fallback in case no
          balancers can be contacted.
     2. The gRPC servers to which the load balancer is directing the client
        may report load to the load balancers, if that information is needed
        by the load balancer's configuration.
     3. The load balancer returns a server list to the gRPC client's `grpclb`
        policy. The `grpclb` policy will then create a subchannel to each of
        server in the list.
4. For each RPC sent, the load balancing policy decides which
   subchannel (i.e., which server) the RPC should be sent to.
   - In the case of the `grpclb` policy, the client will send requests
     to the servers in the order in which they were returned by the load
     balancer.  If the server list is empty, the call will block until a
     non-empty one is received.
