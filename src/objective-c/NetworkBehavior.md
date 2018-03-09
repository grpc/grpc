
# gRPC iOS Network Transition Behaviors
On iOS devices, network transitions happen frequently. A device may have
multiple interfaces to connect to network, and these interfaces may become
broken or alive at any time. This document describes how these network changes
should be handled by gRPC and current issues related.

## Model
We classify network connectivity in three categories: WiFi, cellular, and none.
Currently, the network connectivity information is obtained from
`SCNetworkReachability` API and the category is determined by
`SCNetworkReachabilityFlags` as follows:

| Reachable | ConnectionRequired | IsWAAN | **Category** |
|:---------:|:------------------:|:------:|:------------:|
|     0     |          X         |   X    |     None     |
|     X     |          1         |   X    |     None     |
|     1     |          0         |   0    |     WiFi     |
|     1     |          0         |   1    |   Cellular   |

Whenever there is a switch of network connectivity between these three
categories, the previous channels is assumed to be broken. If there is an
unfinished call, the call should return with status code `UNAVAILABLE`. All
active gRPC channels will be destroyed so that any new call will trigger
creation of new channel over new interface. In addition to that, when a TCP
connection breaks, the corresponding channel should also be destroyed and calls
be canceled with status code `UNAVAILABLE`.

## Known issues
gRPC currently uses BSD sockets for TCP connections. There are several issues
related to BSD sockets known to us that causes problems. gRPC has a plan to
switch to CFStream API for TCP connections which resolves some of these
problems.

* TCP socket stalls but does not return error when network status switches from
  Cellular to WiFi. This problem is workarounded by
  [ConnectivityMonitor](https://github.com/grpc/grpc/blob/master/src/objective-c/GRPCClient/private/GRPCConnectivityMonitor.m).
  The workaround can be discarded with CFStream implementation.
* TCP socket stalls but does not return error when WiFi reconnects to another
  hotspot while the app is in background. This issue is to be resolved by
  CFStream implementation.

Other known issue(s):
* A call does not fail immediately when name resolution fails. The issue is
  being tracked by [#13627](https://github.com/grpc/grpc/issues/13627).
