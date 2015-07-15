GRPC Connection Backoff Protocol
================================

When we do a connection to a backend which fails, it is typically desirable to
not retry immediately (to avoid flooding the network or the server with
requests) and instead do some form of exponential backoff.

We have several parameters:
 1. INITIAL_BACKOFF (how long to wait after the first failure before retrying)
 2. MULTIPLIER (factor with which to multiply backoff after a failed retry)
 3. MAX_BACKOFF (upper bound on backoff)
 4. MIN_CONNECT_TIMEOUT (minimum time we're willing to give a connection to
    complete)

## Proposed Backoff Algorithm

Exponentially back off the start time of connection attempts up to a limit of
MAX_BACKOFF, with jitter.

```
ConnectWithBackoff()
  current_backoff = INITIAL_BACKOFF
  current_deadline = now() + INITIAL_BACKOFF
  while (TryConnect(Max(current_deadline, now() + MIN_CONNECT_TIMEOUT))
         != SUCCESS)
    SleepUntil(current_deadline)
    current_backoff = Min(current_backoff * MULTIPLIER, MAX_BACKOFF)
    current_deadline = now() + current_backoff +
      UniformRandom(-JITTER * current_backoff, JITTER * current_backoff)

```

With specific parameters of
MIN_CONNECT_TIMEOUT = 20 seconds
INITIAL_BACKOFF = 1 second
MULTIPLIER = 1.6
MAX_BACKOFF = 120 seconds
JITTER = 0.2

Implementations with pressing concerns (such as minimizing the number of wakeups
on a mobile phone) may wish to use a different algorithm, and in particular
different jitter logic.

Alternate implementations must ensure that connection backoffs started at the
same time disperse, and must not attempt connections substantially more often
than the above algorithm.
