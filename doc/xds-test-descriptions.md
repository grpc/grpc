# xDS (Load-Balancing) Interop Test Case Descriptions

Client and server use [test.proto](../src/proto/grpc/testing/test.proto).

## Server

The code for the xDS test server can be found at:
[Java](https://github.com/grpc/grpc-java/blob/master/interop-testing/src/main/java/io/grpc/testing/integration/XdsTestServer.java) (other language implementations are in progress).

Server should accept these arguments:

*   --port=PORT
    *   The port the test server will run on.
*   --maintenance_port=PORT
    *   The port for the maintenance server running health, channelz, and admin(CSDS) services.
*   --secure_mode=BOOLEAN
    *   When set to true it uses XdsServerCredentials with the test server for security test cases.
        In case of secure mode, port and maintenance_port should be different.

In addition, when handling requests, if the initial request metadata contains the `rpc-behavior` key, it should modify its handling of the request as follows:

 - If the value matches `sleep-<int>`, the server should wait the specified number of seconds before resuming behavior matching and RPC processing.
 - If the value matches `keep-open`, the server should never respond to the request and behavior matching ends.
 - If the value matches `error-code-<int>`, the server should respond with the specified status code and behavior matching ends.
 - If the value matches `success-on-retry-attempt-<int>`, and the value of the `grpc-previous-rpc-attempts` metadata field is equal to the specified number, the normal RPC processing should resume and behavior matching ends.
 - A value can have a prefix `hostname=<string>` followed by a space. In that case, the rest of the value should only be applied if the specified hostname matches the server's hostname.

The `rpc-behavior` header value can have multiple options separated by commas. In that case, the value should be split by commas and the options should be applied in the order specified. If a request has multiple `rpc-behavior` metadata values, each one should be processed that way in order.

## Client

The base behavior of the xDS test client is to send a constant QPS of unary
messages and record the remote-peer distribution of the responses. Further, the
client must expose an implementation of the `LoadBalancerStatsService` gRPC
service to allow the test driver to validate the load balancing behavior for a
particular test case (see below for more details).

The code for the xDS test client can be at:
[Java](https://github.com/grpc/grpc-java/blob/master/interop-testing/src/main/java/io/grpc/testing/integration/XdsTestClient.java) (other language implementations are in progress).

Clients should accept these arguments:

*   --fail_on_failed_rpcs=BOOL
    *   If true, the client should exit with a non-zero return code if any RPCs
        fail after at least one RPC has succeeded, indicating a valid xDS config
        was received. This accounts for any startup-related delays in receiving
        an initial config from the load balancer. Default is false.
*   --num_channels=CHANNELS
    *   The number of channels to create to the server.
*   --qps=QPS
    *   The QPS per channel.
*   --server=HOSTNAME:PORT
    *   The server host to connect to. For example, "localhost:8080"
*   --stats_port=PORT
    *   The port for to expose the client's `LoadBalancerStatsService`
        implementation.
*   --rpc_timeout_sec=SEC
    *   The timeout to set on all outbound RPCs. Default is 20.
*   --secure_mode=BOOLEAN
    *   When set to true it uses XdsChannelCredentials with the test client for security test cases.

### XdsUpdateClientConfigureService

The xDS test client's behavior can be dynamically changed in the middle of tests.
This is achieved by invoking the `XdsUpdateClientConfigureService` gRPC service
on the test client. This can be useful for tests requiring special client behaviors
that are not desirable at test initialization and client warmup. The service is
defined as:

```
message ClientConfigureRequest {
  // Type of RPCs to send.
  enum RpcType {
    EMPTY_CALL = 0;
    UNARY_CALL = 1;
  }

  // Metadata to be attached for the given type of RPCs.
  message Metadata {
    RpcType type = 1;
    string key = 2;
    string value = 3;
  }

  // The types of RPCs the client sends.
  repeated RpcType types = 1;
  // The collection of custom metadata to be attached to RPCs sent by the client.
  repeated Metadata metadata = 2;
  // The deadline to use, in seconds, for all RPCs.  If unset or zero, the
  // client will use the default from the command-line.
  int32 timeout_sec = 3;
}

message ClientConfigureResponse {}

service XdsUpdateClientConfigureService {
  // Update the tes client's configuration.
  rpc Configure(ClientConfigureRequest) returns (ClientConfigureResponse);
}
```

The test client changes its behavior right after receiving the
`ClientConfigureRequest`. Currently it only supports configuring the type(s) 
of RPCs sent by the test client, metadata attached to each type of RPCs, and the timeout.

## Test Driver

Note that, unlike our other interop tests, neither the client nor the server has
any notion of which of the following test scenarios is under test. Instead, a
separate test driver is responsible for configuring the load balancer and the
server backends, running the client, and then querying the client's
`LoadBalancerStatsService` to validate load balancer behavior for each of the
tests described below.

## LoadBalancerStatsService

The service is defined as:

```
message LoadBalancerStatsRequest {
  // Request stats for the next num_rpcs sent by client.
  int32 num_rpcs = 1;
  // If num_rpcs have not completed within timeout_sec, return partial results.
  int32 timeout_sec = 2;
}

message LoadBalancerStatsResponse {
  message RpcsByPeer {
    // The number of completed RPCs for each peer.
    map<string, int32> rpcs_by_peer = 1;
  }
  // The number of completed RPCs for each peer.
  map<string, int32> rpcs_by_peer = 1;
  // The number of RPCs that failed to record a remote peer.
  int32 num_failures = 2;
  map<string, RpcsByPeer> rpcs_by_method = 3;
}

message LoadBalancerAccumulatedStatsRequest {}

message LoadBalancerAccumulatedStatsResponse {
  // The total number of RPCs have ever issued for each type.
  // Deprecated: use stats_per_method.rpcs_started instead.
  map<string, int32> num_rpcs_started_by_method = 1 [deprecated = true];
  // The total number of RPCs have ever completed successfully for each type.
  // Deprecated: use stats_per_method.result instead.
  map<string, int32> num_rpcs_succeeded_by_method = 2 [deprecated = true];
  // The total number of RPCs have ever failed for each type.
  // Deprecated: use stats_per_method.result instead.
  map<string, int32> num_rpcs_failed_by_method = 3 [deprecated = true];

  message MethodStats {
    // The number of RPCs that were started for this method.
    int32 rpcs_started = 1;

    // The number of RPCs that completed with each status for this method.  The
    // key is the integral value of a google.rpc.Code; the value is the count.
    map<int32, int32> result = 2;
  }

  // Per-method RPC statistics.  The key is the RpcType in string form; e.g.
  // 'EMPTY_CALL' or 'UNARY_CALL'
  map<string, MethodStats> stats_per_method = 4;
}

service LoadBalancerStatsService {
  // Gets the backend distribution for RPCs sent by a test client.
  rpc GetClientStats(LoadBalancerStatsRequest)
      returns (LoadBalancerStatsResponse) {}
  // Gets the accumulated stats for RPCs sent by a test client.
  rpc GetClientAccumulatedStats(LoadBalancerAccumulatedStatsRequest)
      returns (LoadBalancerAccumulatedStatsResponse) {}
}
```

Note that the `LoadBalancerStatsResponse` contains the remote peer distribution
of the next `num_rpcs` *sent* by the client after receiving the
`LoadBalancerStatsRequest`. It is important that the remote peer distribution be
recorded for a block of consecutive outgoing RPCs, to validate the intended
distribution from the load balancer, rather than just looking at the next
`num_rpcs` responses received from backends, as different backends may respond
at different rates.

## Test Cases

### ping_pong

This test verifies that every backend receives traffic.

Client parameters:

1.  --num_channels=1
1.  --qps=100
1.  --fail_on_failed_rpc=true

Load balancer configuration:

1.  4 backends are created in a single managed instance group (MIG).

Test driver asserts:

1.  All backends receive at least one RPC

### round_robin

This test verifies that RPCs are evenly routed according to an unweighted round
robin policy.

Client parameters:

1.  --num_channels=1
1.  --qps=100
1.  --fail_on_failed_rpc=true

Load balancer configuration:

1.  4 backends are created in a single MIG.

Test driver asserts that:

1.  Once all backends receive at least one RPC, the following 100 RPCs are
    evenly distributed across the 4 backends.

### backends_restart

This test verifies that the load balancer will resume sending traffic to a set
of backends that is stopped and then resumed.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  4 backends are created in a single MIG.

Test driver asserts:

1.  All backends receive at least one RPC.

The test driver records the peer distribution for a subsequent block of 100 RPCs
then stops the backends.

Test driver asserts:

1.  No RPCs from the client are successful.

The test driver resumes the backends.

Test driver asserts:

1.  Once all backends receive at least one RPC, the distribution for a block of
    100 RPCs is the same as the distribution recorded prior to restart.

### secondary_locality_gets_requests_on_primary_failure

This test verifies that backends in a secondary locality receive traffic when
all backends in the primary locality fail.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  The primary MIG with 2 backends in the same zone as the client
1.  The secondary MIG with 2 backends in a different zone

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  No backends in the secondary locality receive RPCs.

The test driver stops the backends in the primary locality.

Test driver asserts:

1.  All backends in the secondary locality receive at least 1 RPC.

The test driver resumes the backends in the primary locality.

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  No backends in the secondary locality receive RPCs.

### secondary_locality_gets_no_requests_on_partial_primary_failure

This test verifies that backends in a failover locality do not receive traffic
when at least one of the backends in the primary locality remain healthy.

**Note:** Future TD features may change the expected behavior and require
changes to this test case.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  The primary MIG with 2 backends in the same zone as the client
1.  The secondary MIG with 2 backends in a different zone

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  No backends in the secondary locality receive RPCs.

The test driver stops one of the backends in the primary locality.

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  No backends in the secondary locality receive RPCs.

### remove_instance_group

This test verifies that a remaining instance group can successfully serve RPCs
after removal of another instance group in the same zone.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  Two MIGs with two backends each, using rate balancing mode.

Test driver asserts:

1.  All backends receive at least one RPC.

The test driver removes one MIG.

Test driver asserts:

1.  All RPCs are directed to the two remaining backends (no RPC failures).

### change_backend_service

This test verifies that the backend service can be replaced and traffic routed
to the new backends.

Client parameters:

1.  --num_channels=1
1.  --qps=100
1.  --fail_on_failed_rpc=true

Load balancer configuration:

1.  One MIG with two backends

Test driver asserts:

1.  All backends receive at least one RPC.

The test driver creates a new backend service containing a MIG with two backends
and changes the TD URL map to point to this new backend service.

Test driver asserts:

1.  All RPCs are directed to the new backend service.

### traffic_splitting

This test verifies that the traffic will be distributed between backend
services with the correct weights when route action is set to weighted
backend services.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  One MIG with one backend

Assert:

1. Once all backends receive at least one RPC, the following 1000 RPCs are
all sent to MIG_a.

The test driver adds a new MIG with 1 backend, and changes the route action
to weighted backend services with {a: 20, b: 80}.

Assert:

1. Once all backends receive at least one RPC, the following 1000 RPCs are
distributed across the 2 backends as a: 20, b: 80.
### path_matching

This test verifies that the traffic for a certain RPC can be routed to a
specific cluster based on the RPC path.

Client parameters:

1.  –num_channels=1
1.  –qps=10
1.  –fail_on_failed_rpc=true
1.  –rpc=“EmptyCall,UnaryCall”

Load balancer configuration:

1.  2 MIGs, each with 1 backend
1.  routes
    - “/”: MIG_default

Assert:

1.  UnaryCall RPCs are sent to MIG_default
1.  EmptyCall RPCs are sent to MIG_default

The test driver changes route and asserts RPCs are sent to expected backends. **Note** that the default route `"/"` is always pointing to MIG_default, so all RPCs not matching the new route will be sent to MIG_default.

- {path: `/grpc.testing.TestService/EmptyCall`}: MIG_2
  - UnaryCall -> MIG_default
  - EmptyCall -> MIG_2

- {prefix: `/grpc.testing.TestService/Unary`}: MIG_2
  - UnaryCall -> MIG_2
  - EmptyCall -> MIG_default

- {prefix: `/grpc.testing.TestService/Unary`}: MIG_default & {path: `/grpc.testing.TestService/EmptyCall`}: MIG_2
  - UnaryCall -> MIG_default
  - EmptyCall -> MIG_2

- {regex: `^\/.*\/UnaryCall$`}: MIG_2
  - UnaryCall -> MIG_2
  - EmptyCall -> MIG_default

- {path: `/gRpC.tEsTinG.tEstseRvice/empTycaLl`, ignoreCase: `True`}: MIG_2
  - UnaryCall -> MIG_default
  - EmptyCall -> MIG_2

### header_matching

This test verifies that the traffic for a certain RPC can be routed to a
specific cluster based on the RPC header (metadata).

Client parameters:

1.  –num_channels=1
1.  –qps=10
1.  –fail_on_failed_rpc=true
1.  –rpc=“EmptyCall,UnaryCall”
1.  –rpc=“EmptyCall:xds_md:exact_match”

Load balancer configuration:

1.  2 MIGs, each with 1 backend
1.  routes
    - “/”: MIG_default

Assert:

1.  UnaryCall RPCs are sent to MIG_default
1.  EmptyCall RPCs are sent to MIG_default

The test driver changes route and asserts RPCs are sent to expected backends. **Note** that the default route `"/"` is always pointing to MIG_default, so all RPCs not matching the new route will be sent to MIG_default.

- {header `xds_md`, exact: `empty_ytpme`}: MIG_2
	- Unary -> MIG_default
	- Empty -> MIG_2

- {header `xds_md`, prefix: `un`}: MIG_2
	- `un` is the prefix of metadata sent with UnaryCall
	- Unary -> MIG_2
	- Empty -> MIG_default

- {header `xds_md`, suffix: `me`}: MIG_2
	- `me` is the suffix of metadata sent with EmptyCall
	- Unary -> MIG_default
	- Empty to MIG_2

- {header `xds_md_numeric`, present: `True`}: MIG_2
	- Unary is sent with the metadata, so will be sent to alternative
	- Unary -> MIG_2
	- Empty -> MIG_default

- {header `xds_md`, exact: `unary_yranu`, invert: `True`}: MIG_2
	- Unary is sent with the metadata, so this will not match Unary, but will match Empty
	- Unary -> MIG_default
	- Empty to MIG_2

- {header `xds_md_numeric`, range `[100,200]`}: MIG_2
	- Unary is sent with the metadata in range
	- Unary -> MIG_2
	- Empty -> MIG_default

- {header `xds_md`, regex: `^em.*me$`}: MIG_2
	- EmptyCall is sent with the metadata
	- Unary -> MIG_default
	- Empty -> MIG_2

### gentle_failover

This test verifies that traffic is partially diverted to a secondary locality
when > 50% of the instances in the primary locality are unhealthy.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  The primary MIG with 3 backends in the same zone as the client
1.  The secondary MIG with 2 backends in a different zone

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  No backends in the secondary locality receive RPCs.

The test driver stops 2 of 3 backends in the primary locality.

Test driver asserts:

1.  All backends in the secondary locality receive at least 1 RPC.
1.  The remaining backend in the primary locality receives at least 1 RPC.

The test driver resumes the backends in the primary locality.

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  No backends in the secondary locality receive RPCs.


### load_based_failover

This test verifies that traffic is partially diverted to a secondary locality
when the QPS is greater than the configured RPS in the priority locality.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  The primary MIG with 2 backends in the same zone as the client
1.  The secondary MIG with 2 backends in a different zone

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  No backends in the secondary locality receive RPCs.

The test driver sets `balancingMode` is `RATE`, and `maxRate` to 20 in the primary locality.

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  All backends in the secondary locality receive at least 1 RPC.

The test driver set `maxRate` to 120 in the primary locality.

Test driver asserts:

1.  All backends in the primary locality receive at least 1 RPC.
1.  No backends in the secondary locality receive RPCs.


### circuit_breaking

This test verifies that the maximum number of outstanding requests is limited
by circuit breakers of the backend service.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  Two MIGs with each having two backends.

The test driver configures the backend services with:

1. path{“/grpc.testing.TestService/UnaryCall"}: MIG_1
1. path{“/grpc.testing.TestService/EmptyCall"}: MIG_2
1. MIG_1 circuit_breakers with max_requests = 500
1. MIG_2 circuit breakers with max_requests = 1000

The test driver configures the test client to send both UnaryCall and EmptyCall,
with all RPCs keep-open.

Assert:

1.  After reaching steady state, there are 500 UnaryCall RPCs in-flight
and 1000 EmptyCall RPCs in-flight.

The test driver updates MIG_1's circuit breakers with max_request = 800.

Test driver asserts:

1.  After reaching steady state, there are 800 UnaryCall RPCs in-flight.

### timeout

This test verifies that traffic along a route with a `max_stream_duration` set
will cause timeouts on streams open longer than that duration.

Client parameters:

1. `--num_channels=1`
1. `--qps=100`

Route Configuration:

Two routes:

1. Path match for `/grpc.testing.TestService/UnaryCall`, with a `route_action`
   containing `max_stream_duration` of 3 seconds.
1. Default route containing no `max_stream_duration` setting.

There are four sub-tests:

1. `app_timeout_exceeded`
   1. Test client configured to send UnaryCall RPCs with a 1s application
      timeout, and metadata of `rpc-behavior: sleep-2`.
   1. Test driver asserts client recieves ~100% status `DEADLINE_EXCEEDED`.
1. `timeout_not_exceeded`
   1. Test client configured to send UnaryCall RPCs with the default
      application timeout (20 seconds), and no metadata.
   1. Test driver asserts client recieves ~100% status `OK`.
1. `timeout_exceeded` (executed with the below test case)
1. `timeout_different_route`
   1. Test client configured to send UnaryCall RPCs and EmptyCall RPCs with
      the default application timeout (20 seconds), and metadata of
      `rpc-behavior: sleep-4`.
   1. Test driver asserts client recieves ~100% status `OK` for EmptyCall
      and ~100% status `DEADLINE_EXCEEDED` for UnaryCall.

### api_listener
The test case verifies a specific use case where it creates a second TD API 
listener using the same name as the existing one and then delete the old one. 
The test driver verifies this is a safe way to update the API listener 
configuration while keep using the existing name.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  One MIG with two backends.

Assert:

The test driver configuration steps:
1. The test driver creates the first set of forwarding rule + target proxy + 
URL map with a test host name.
1. Then the test driver creates a second set of forwarding rule + target proxy + 
URL map with the same test host name.
1. The test driver deletes the first set of configurations in step 1.

The test driver verifies, at each configuration step, the traffic is always able 
to reach the designated hosts.

### metadata_filter
This test case verifies that metadata filter configurations in URL map match 
rule are effective at Traffic Director for routing selection against downstream
node metadata.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  Two MIGs in the same zone, each having two backends.

There are four test sub-cases:
1. Test `MATCH_ALL` metadata filter criteria.
1. Test `MATCH_ANY` metadata filter criteria.
1. Test mixed `MATCH_ALL` and `MATCH_ANY` metadata filter criteria.
1. Test when multiple match rules with metadata filter all match.

Assert:

At each test sub-case described above, the test driver configures
and verifies:

1. Set default URL map, and verify traffic goes to the original backend hosts. 
1. Then patch URL map to update the match rule with metadata filter 
configuration under test added.
1. Then it verifies traffic switches to alternate backend service hosts. 

This way, we test that TD correctly evaluates both matching and non-matching 
configuration scenario.

### forwarding_rule_port_match
This test verifies that request server uri port should match with the GCP 
forwarding rule configuration port.

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  One MIG with two backends.

Assert:
1. The test driver configures matching port in the forwarding rule and in the
request server uri, then verifies traffic reaches backend service instances.
1. The test driver updates the forwarding rule to use a different port, then 
verifies that the traffic stops going to those backend service instances.

### forwarding_rule_default_port
This test verifies that omitting port in the request server uri should only 
match with the default port(80) configuration in the forwarding rule. 
In addition, request server uri port should exactly match that in the URL map 
host rule, as described in 
[public doc](https://cloud.google.com/traffic-director/docs/proxyless-overview#proxyless-url-map).

Client parameters:

1.  --num_channels=1
1.  --qps=100

Load balancer configuration:

1.  One MIG with two backends.

Assert:

Test driver configures and verifies:
1. No traffic goes to backends when configuring the target URI 
`xds:///myservice`, the forwarding rule with port *x != 80*, the URL map 
host rule `myservice::x`.
1. Traffic goes to backends when configuring the target URI `xds:///myservice`, 
the forwarding rule port `80` and the URL map host rule `myservice`.
1. No traffic goes to backends when configuring the target URI
`xds:///myservice`, the forwarding rule port `80` and the host rule 
`myservice::80`.

### outlier_detection
This test verifies that the client applies the outlier detection configuration
and temporarily drops traffic to a server that fails requests.

Client parameters:

1.  --num_channels=1
2.  --qps=100

Load balancer configuration:

1.  One MIG with five backends, with a `backendService` configuration with the
    following `outlierDetection` entry
    ```json
    {
      "interval": {
        "seconds": 2,
        "nanos": 0
      },
      "successRateRequestVolume": 20
    }
    ```
Assert:

1.  The test driver asserts that traffic is equally distribted among the
five backends, and all requests end with the `OK` status.
2.  The test driver chooses one of the five backends to fail requests, and
configures the client to send the metadata
`rpc-behavior: hostname=<chosen backend> error-code-2`. The driver asserts
that during some 10-second interval, all traffic goes to the other four
backends and all requests end with the `OK` status.
3.  The test driver removes the client configuration to send metadata. The
driver asserts that during some 10-second interval, traffic is equally
distributed among the five backends, and all requests end with the `OK` status.

### custom_lb
This test verifies that a custom load balancer policy can be configured in the
client. It also verifies that when given a list of policies the client can
ignore a bad one and try the next one on the list until it finds a good one.

Client parameters:

1.  --num_channels=1
2.  --qps=100

Load balancer configuration:

One MIG with a single backend.

The `backendService` will have the following `localityLbPolicies` entry:
```json
[ 
  {
    "customPolicy": {
      "name": "test.ThisLoadBalancerDoesNotExist",
      "data": "{ \"foo\": \"bar\" }"
    }
  },
  {
    "customPolicy": {
      "name": "test.RpcBehaviorLoadBalancer",
      "data": "{ \"rpcBehavior\": \"error-code-15\" }"
    }
  }
]
```

The client **should not** implement the `test.ThisLoadBalancerDoesNotExist`, but
it **should** implement `test.RpcBehaviorLoadBalancer`. The
`RpcBehaviorLoadBalancer` implementation should set the rpcBehavior request
header based on the configuration it is provided. The `rpcBehavior` field value
in the config should be used as the header value.

Assert:

1. The first custom policy is ignored as the client does not have an
implementation for it.
2. The second policy, that **is** implemented by the client, has been applied
by the client. This can be asserted by confirming that each request has
failed with the configured error code 15 (DATA_LOSS). We should get this error
because the test server knows to look for the `rpcBehavior` header and fail
a request with a provided error code.

Note that while this test is for load balancing, we can get by with a single
backend as our test load balancer does not perform any actual load balancing,
instead only applying the `rpcBehavior` header to each request.
