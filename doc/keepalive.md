# Keepalive Ping User Guide for gRPC Core (and dependants)

The keepalive ping is a way to check if a channel is currently working by sending HTTP2 pings over the transport. It is sent periodically, and if the ping is not acknowledged by the peer within a certain timeout period, the transport is disconnected.

This guide documents the knobs within gRPC core to control the current behavior of the keepalive ping.

The keepalive ping is controlled by two important channel arguments -
* **GRPC_ARG_KEEPALIVE_TIME_MS**
  * This channel argument controls the period (in milliseconds) after which a keepalive ping is sent on the transport.
* **GRPC_ARG_KEEPALIVE_TIMEOUT_MS**
  * This channel argument controls the amount of time (in milliseconds), the sender of the keepalive ping waits for an acknowledgement. If it does not receive an acknowledgement within this time, it will close the connection.

The above two channel arguments should be sufficient for most users, but the following arguments can also be useful in certain use cases.
* **GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS**
  * This channel argument if set to 1 (0 : false; 1 : true), allows keepalive pings to be sent even if there are no calls in flight. 
* **GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA**
  * This channel argument controls the maximum number of pings that can be sent when there is no other data (data frame or header frame) to be sent. GRPC Core will not continue sending pings if we run over the limit
* **GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS**
  * If there is no data being sent on the transport, this channel argument controls the minimum time (in milliseconds) gRPC Core will wait between successive pings.
**GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS**
  * If there is no data being sent on the transport, this channel argument on the server side controls the minimum time (in milliseconds) that gRPC Core would expect between receiving successive pings. If the time between successive pings is less that than this time, then the ping will be considered a bad ping from the peer. Such a ping counts as a ‘ping strike’.
On the client side, this does not have any effect.
* **GRPC_ARG_HTTP2_MAX_PING_STRIKES**
  * This arg controls the maximum number of bad pings that the server will tolerate before sending an HTTP2 GOAWAY frame and closing the transport. Setting it to 0 allows the server to accept any number of bad pings.

### Defaults Values

Channel Argument| Client|Server
----------------|-------|------
GRPC_ARG_KEEPALIVE_TIME_MS|INT_MAX (disabled)|7200000 (2 hours)
GRPC_ARG_KEEPALIVE_TIMEOUT_MS|20000 (20 seconds)|20000 (20 seconds)
GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS|0 (false)|0 (false)
GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA|2|2
GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS|300000 (5 minutes)|300000 (5 minutes)
GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS|N/A|300000 (5 minutes)
GRPC_ARG_HTTP2_MAX_PING_STRIKES|N/A|2
