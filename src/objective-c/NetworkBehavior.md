
# gRPC iOS Network Transition Behaviors
On iOS devices, network transitions happen frequently. A device may have
multiple interfaces to connect to network, and these interfaces may become
broken or alive at any time. This document describes how these network changes
should be handled by gRPC and current issues related.

## gRPC iOS with TCP Sockets
### Model
We classify network connectivity of the device at a given time in three
categories: WiFi, cellular, and none.  The network connectivity information is
obtained from `SCNetworkReachability` API and the category is determined by
`SCNetworkReachabilityFlags` as follows:

| Reachable | ConnectionRequired | IsWAAN | **Category** |
|:---------:|:------------------:|:------:|:------------:|
|     0     |          X         |   X    |     None     |
|     X     |          1         |   X    |     None     |
|     1     |          0         |   0    |     WiFi     |
|     1     |          0         |   1    |   Cellular   |

Whenever there is a transition of network between these three categories, the
previous channels is assumed to be broken. If there is an unfinished call, the
call should return with status code `UNAVAILABLE`. All active gRPC channels
will be destroyed so that any new call will trigger creation of new channel
over new interface. In addition to that, when a TCP connection breaks, the
corresponding channel should also be destroyed and calls be canceled with
status code `UNAVAILABLE`.

### Known issues
There are several issues related to BSD sockets known to us that causes
problems. 

* TCP socket stalls but does not return error when network status transits from
  Cellular to WiFi. This problem is workarounded by
  [ConnectivityMonitor](https://github.com/grpc/grpc/blob/master/src/objective-c/GRPCClient/private/GRPCConnectivityMonitor.m).
* TCP socket stalls but does not return error when WiFi reconnects to another
  hotspot while the app is in background.

If you encounter these problems, the best solution is to switch to CFStream
implementation which eliminates all of them.

## gRPC iOS with CFStream
gRPC iOS with CFStream implementation uses Apple's networking API to make
connections. It resolves the issues above that is known to TCP sockets on iOS.
Users are recommended to use this implementation rather than TCP socket
implementation. With CFStream implementation, a channel is broken when the
underlying stream detects an error or becomes closed. The behavior of streams
in CFStream are not documented by Apple, but our experiments show that it is
very similar to the model above, i.e. the streams error out when there is a
network connetivity change. So users should expect channels to break when the
network transits to another state and pending calls on those channels return
with status code `UNAVAILABLE`.
