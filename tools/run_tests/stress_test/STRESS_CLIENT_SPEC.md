Stress Test client Specification
=========================
This document specifies the features a stress test client should implement in order to work with the stress testing framework. The stress test clients are executed against the existing interop test servers.

**Requirements**
--------------
**1.** A stress test client should be able to repeatedly execute one or more of the existing 'interop test cases'. It may just be a wrapper around the existing interop test client. The exact command line arguments the client should support are listed in _Table 1_ below.

**2.** The stress test client must implement a metrics server defined by _[metrics.proto](https://github.com/grpc/grpc/blob/master/src/proto/grpc/testing/metrics.proto)_ and must expose _qps_ as a `Long`-valued Gauge. The client can track the overall _qps_ in one Gauge or in multiple Gauges (for example: One per Channel or Stub).
 The framework periodically queries the _qps_ by calling the `GetAllGauges()` method (the framework assumes that all the returned Gauges are _qps_ Gauges and adds them up to determine the final qps) and uses this to determine if the stress test client is running or crashed or stalled.
> *Note:* In this context, the term  _**qps**_  means _interop test cases per second_  (not _messages per second_ or _rpc calls per second_)


**Table 1:** Command line arguments that should be supported by the stress test client.

>_**Note** The current C++ [stress client](https://github.com/grpc/grpc/blob/master/test/cpp/interop/stress_test.cc) supports more flags than those listed here but those flags will soon be deprecated._

Parameter             |                    Description
----------------------|---------------------------------
`--server_addresses`    | The stress client should accept a list of server addresses in the following format:<br> ```<name_1>:<port_1>,<name_2>:<port_2>..<name_N>:<port_N>``` <br> _Note:_ `<name>` can be either server name or IP address.<br><br>_Type:_ string <br>_default:_ ```localhost:8080``` <br>_Example:_ ``foo.foobar.com:8080,bar.foobar.com:8080`` <br><br> Currently, the stress test framework only passes one server address to the client.
`--test_cases`        |   List of test cases along with the relative weights in the following format:<br> `<testcase_1:w_1>,<testcase_2:w_2>...<testcase_n:w_n>`. <br> The test cases names are the same as those currently used by the interop clients<br><br>_Type:_ string <br>_Example:_ `empty_unary:20,large_unary:10,empty_stream:70` <br>(The stress client would then make `empty_unary` calls 20% of the time, `large_unary` calls 10% of the time and `empty_stream` calls 70% of the time.) <br>_Note:_ The weights need not add up to 100.
`--test_duration_secs`      | The test duration in seconds. A value of -1 means that the test should run forever until forcefully terminated. <br>_Type:_ int <br>_default:_ -1
`--num_channels_per_server` | Number of channels (i.e connections) to each server. <br> _Type:_ int <br> _default:_ 1 <br><br> _Note:_ Unfortunately, the term `channel` is used differently in `grpc-java` and `C based grpc`. In this context, this really means "number of connections to the server"
`--num_stubs_per_channel `  | Number of client stubs per each connection to server.<br>_Type:_ int <br>_default:_ 1
`--metrics_port`            | The port at which the stress client exposes [QPS metrics](https://github.com/grpc/grpc/blob/master/src/proto/grpc/testing/metrics.proto). <br>_Type:_ int <br>_default:_ 8081
