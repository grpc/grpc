Load Balancing in gRPC
=======================

# Objective

To design a load balancing API between a gRPC client and a Load Balancer to
instruct the client how to send load to multiple backend servers. 

# Background

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
well-known algorithms (ie, Round Robin) for server selection.
Complex load balancing algorithms are instead provided by the load balancer. The
client relies on the load balancer to provide _load balancing configuration_ and
_the list of servers_ to which the client should send requests. The balancer
updates the server list as needed to balance the load as well as handle server
unavailability or health issues. The load balancer will make any necessary
complex decisions and inform the client. The load balancer may communicate with
the backend servers to collect load and health information.

# Proposed Architecture

The gRPC load balancing approach follows the third approach, by having an
external load balancer which provides simple clients with a list of servers.

## Client

When establishing a gRPC stream to the balancer, the client will send an initial
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

### Security 

The load balancer may be separate from the actual server backends and a
compromise of the load balancer should only lead to a compromise of the
loadbalancing functionality. In other words, a compromised load balancer should
not be able to cause a client to trust a (potentially malicious) backend server
any more than in a comparable situation without loadbalancing. 
