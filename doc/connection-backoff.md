GRPC Connection Backoff Protocol
================================

When we do a connection to a backend which fails, it is typically desirable to
not retry immediately (to avoid flooding the network or the server with
requests) and instead do some form of exponential backoff.

We have several parameters:
 1. INITIAL_BACKOFF (how long to wait after the first failure before retrying)
 2. MULTIPLIER (factor with which to multiply backoff after a failed retry)
 3. MAX_BACKOFF (Upper bound on backoff)
 4. MIN_CONNECTION_TIMEOUT

## Proposed Backoff Algorithm

Exponentially back off the start time of connection attempts up to a limit of
MAX_BACKOFF.

```
ConnectWithBackoff()
  current_backoff = INITIAL_BACKOFF
  current_deadline = now() + INITIAL_BACKOFF
  while (TryConnect(Max(current_deadline, MIN_CONNECT_TIMEOUT))
         != SUCCESS)
    SleepUntil(current_deadline)
    current_backoff = Min(current_backoff * MULTIPLIER, MAX_BACKOFF)
    current_deadline = now() + current_backoff
```

## Historical Algorithm in Stubby

Exponentially increase up to a limit of MAX_BACKOFF the intervals between
connection attempts. This is what stubby 2 uses, and is equivalent if
TryConnect() fails instantly.

```
LegacyConnectWithBackoff()
  current_backoff = INITIAL_BACKOFF
  while (TryConnect(MIN_CONNECT_TIMEOUT) != SUCCESS)
    SleepFor(current_backoff)
    current_backoff = Min(current_backoff * MULTIPLIER, MAX_BACKOFF)
```

The grpc C implementation currently uses this approach with an initial backoff
of 1 second, multiplier of 2, and maximum backoff of 120 seconds. (This will
change)

Stubby, or at least rpc2, uses exactly this algorithm with an initial backoff
of 1 second, multiplier of 1.2, and a maximum backoff of 120 seconds.

## Use Cases to Consider

* Client tries to connect to a server which is down for multiple hours, eg for
  maintenance
* Client tries to connect to a server which is overloaded
* User is bringing up both a client and a server at the same time
    * In particular, we would like to avoid a large unnecessary delay if the
      client connects to a server which is about to come up
* Client/server are misconfigured such that connection attempts always fail
    * We want to make sure these don’t put too much load on the server by
      default.
* Server is overloaded and wants to transiently make clients back off
* Application has out of band reason to believe a server is back
    * We should consider an out of band mechanism for the client to hint that
      we should short circuit the backoff.
