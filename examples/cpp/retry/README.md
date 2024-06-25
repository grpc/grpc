# Retry

This example shows how to enable and configure retry on gRPC clients.

## Documentation

[gRFC for client-side retry support](https://github.com/grpc/proposal/blob/master/A6-client-retries.md)

## Try it

This example includes a service implementation that fails requests three times with status
code `Unavailable`, then passes the fourth.  The client is configured to make four retry attempts
when receiving an `Unavailable` status code.

First start the server:

```bash
$ ./server
```

Then run the client:

```bash
$ ./client
```

Expected server output:

```
Server listening on 0.0.0.0:50052
return UNAVAILABLE
return UNAVAILABLE
return UNAVAILABLE
return OK
```

Expected client output:

```
Greeter received: Hello world
```

## Usage

### Define your retry policy

Retry is enabled via the service config, which can be provided by the name resolver or
a [GRPC_ARG_SERVICE_CONFIG](https://github.com/grpc/grpc/blob/master/include/grpc/impl/channel_arg_names.h#L207-L209) channel argument.  In the below config, we set retry policy for the "helloworld.Greeter" service.

`maxAttempts`: how many times to attempt the RPC before failing.

`initialBackoff`, `maxBackoff`, `backoffMultiplier`: configures delay between attempts.

`retryableStatusCodes`: Retry only when receiving these status codes.

```c++
constexpr absl::string_view kRetryPolicy =
    "{\"methodConfig\" : [{"
    "   \"name\" : [{\"service\": \"helloworld.Greeter\"}],"
    "   \"waitForReady\": true,"
    "   \"retryPolicy\": {"
    "     \"maxAttempts\": 4,"
    "     \"initialBackoff\": \"1s\","
    "     \"maxBackoff\": \"120s\","
    "     \"backoffMultiplier\": 1.0,"
    "     \"retryableStatusCodes\": [\"UNAVAILABLE\"]"
    "    }"
    "}]}";
```
