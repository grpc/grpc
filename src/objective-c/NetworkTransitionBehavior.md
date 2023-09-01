
# gRPC iOS Network Transition Behaviors
Network connectivity on an iOS device may transition between cellular, WIFI, or
no network connectivity. This document describes how these network changes
should be handled by gRPC and current known issues.

## Expected Network Transition Behaviors
The expected gRPC iOS channel and network transition behaviors are:
* Channel connection to a particular host is established at the time of
  starting the first call to the channel and remains connected for future calls
  to the same host.
* If the underlying connection to the remote host is broken, the channel is
  disconnected and enters TRANSIENT\_FAILURE state.
* A channel is broken if the channel connection is no longer viable. This
  happens when
    * The network interface is no longer available, e.g. WiFi or cellular
      interface is turned off or goes offline, airplane mode turned on, etc;
    * The underlying TCP connection is no longer valid, e.g. WiFi connects to
      another hotspot, cellular data switched from LTE to 4G, etc;
    * A network interface more preferable by the OS is valid, e.g. WiFi gets
      connected when the channel is already connected via cellular.
* A channel in TRANSIENT\_FAILURE state attempts reconnection on start of the
  next call to the same host, but only after a certain backoff period (see
  corresponding
  [doc](https://github.com/grpc/grpc/blob/master/doc/connection-backoff.md)).
  During the backoff period, any call to the same host will wait until the
  first of the following events occur:
    * Connection succeeded; calls will be made using this channel;
    * Connection failed; calls will be failed and return UNAVAILABLE status code;
    * The call's deadline is reached; the call will fail and return
      DEADLINE\_EXCEEDED status code.
  The length of backoff period of a channel is reset whenever a connection
  attempt is successful.

## Implementations
### gRPC iOS with TCP Sockets
gRPC's default implementation is to use TCP sockets for networking. It turns
out that although Apple supports this type of usage, it is [not recommended by
Apple](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/NetworkingOverview/SocketsAndStreams/SocketsAndStreams.html)
and has some issues described below.

#### Issues with TCP Sockets
The TCP sockets on iOS is flawed in that it does not reflect the viability of
the channel connection. Particularly, we observed the following issues when
using TCP sockets:
* When a TCP socket connection is established on cellular data and WiFi
  becomes available, the TCP socket neither return an error event nor continue
  sending/receiving data on it, but still accepts write on it.
* A TCP socket does not report certain events that happen in the
  background. When a TCP connection breaks in the background for the reason
  like WiFi connects to another hotspot, the socket neither return an error nor
  continue sending/receiving data on it, but still accepts write on it.
In both situations, the user will see the call freeze for an extended period of
time before the TCP socket times out.

#### gRPC iOS library's resolution to TCP socket issues
We introduced
[`ConnectivityMonitor`](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/NetworkingOverview/SocketsAndStreams/SocketsAndStreams.html)
in gRPC iOS library v0.14.0 to alleviate these issues in TCP sockets,
which changes the network transition behaviors a bit.

We classify network connectivity state of the device into three categories
based on flags obtained from `SCNetworkReachability` API:

| Reachable | ConnectionRequired | IsWWAN | **Category** |
|:---------:|:------------------:|:------:|:------------:|
|     0     |          X         |   X    |     None     |
|     X     |          1         |   X    |     None     |
|     1     |          0         |   0    |     WiFi     |
|     1     |          0         |   1    |   Cellular   |

Whenever there is a transition of network between two of these categories, all
previously existing channels are assumed to be broken and are actively
destroyed. If there is an unfinished call, the call should return with status
code `UNAVAILABLE`.

`ConnectivityMonitor` is able to detect the scenario of the first issue above
and actively destroy the channels. However, the second issue is not resolvable.
To solve that issue the best solution is to switch to CFStream implementation
which eliminates all of them.

### gRPC iOS with CFStream
gRPC iOS with CFStream implementation (introduced in v1.13.0) uses Apple's
networking API to make connections. It resolves the issues with TCP sockets
mentioned above. Users are recommended to use this implementation rather than
TCP socket implementation. The detailed behavior of streams in CFStream is not
documented by Apple, but our experiments show that it accords to the expected
behaviors.  With CFStream implementation, an event is always received when the
underlying connection is no longer viable. For more detailed information and
usages of CFStream implementation, refer to the
[user guide](https://github.com/grpc/grpc/blob/master/src/objective-c/README-CFSTREAM.md).

