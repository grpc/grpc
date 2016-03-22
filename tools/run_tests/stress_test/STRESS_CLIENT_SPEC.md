Stress Test client Specification
=========================
This document specifies the features a stress test client should implement to be able to work with the stress testing framework. The stress tests use the existing interop test servers and require no changes on the server side.

**Requirements**
--------------
**1:** A stress test client should be able to repeatedly execute one or more of the existing 'interop test cases'. It can just be a wrapper around the existing interop test client and need not re-implement the interop test cases. The exact arguments the client should take are listed in _Table 1_ below

**2:** The stress test client must implement the metrics server defined by _[metrics.proto](https://github.com/grpc/grpc/blob/master/src/proto/grpc/testing/metrics.proto)_.  The framework mainly uses this as a mechanism to determine if the stress test client is running (or crashed / stalled) by periodically querying the qps.

>**Note:** In this context  _**qps**_  means _interop test cases per second_  (not _messages per second_ or _rpc calls per second_)


**Table 1:** Command line arguments that should be accepted by the stress test client.

>_**Note** The current C++ [stress client](https://github.com/grpc/grpc/blob/master/test/cpp/interop/stress_test.cc) supports more flags than those listed here but those flags will soon be deprecated_|

Parameter             |                    Description
----------------------|---------------------------------
`--server_address`    | The stress client should accept a list of servers (and ports) in the following format and run the tests against all servers. <br> ```<name_1>:<port_1>,<name_2>:<port_2>..<name_N>:<port_N>``` <br> _Note:_ `<name>` can be either server name or IP address. <br><br> _Type:_ string <br> _default:_ ```localhost:8080``` <br> _Example:_ ``foo.foobar.com:8080,bar.foobar.com:8080`` <br><br> Currently, the stress test framework only passes one server address to the client. So it is okay for the stress client to accept only one server address for now
`--test_cases`        |   List of test cases to call along with the relative weights in the following format: <br> `<testcase_1:w_1>,<testcase_2:w_2>...<testcase_n:w_n>`. <br>  The test cases names are those currently used by the interop client <br><br> _Type:_ string <br> _Example:_ `empty_unary:20,large_unary:10,empty_stream:70` <br><br> (The stress client would then make `empty_unary` calls 20% of the time, `large_unary` calls 10% of the time and `empty_stream` calls 70% of the time.) <br> (_Note:_ The weights need not add up to 100)
`--test_duration-secs`     | The length of time (in seconds) to run the test. Enter -1 if the test should run forever until forcefully terminated. <br> _Type:_ int <br> _default:_ -1
`--num_channels_per_server` |        Number of channels (i.e connections) to each server. <br> _Type:_ int <br> _default:_ 1 <br><br> _Note:_ Unfortunately, the term `channel` is used differently in `grpc-java`. In this context, this really means "number of connections to the server" 
`--num_stubs_per_channel `  |  Number of stubs per each connection to server.  <br> _Type:_ int <br> _default:_ 1
`--metrics_port`            | The port on which the stress client exposes the [QPS metrics](https://github.com/grpc/grpc/blob/master/src/proto/grpc/testing/metrics.proto). <br> _Type:_ int <br> _default:_ 8081 <br><br> _Note:_ This is mainly needed for the framework to know the health of stress test client.
                           `--server_address`    | The stress client should accept a list of servers (and ports) in the following format and run the tests against all servers. <br> ```<name_1>:<port_1>,<name_2>:<port_2>..<name_N>:<port_N>``` <br> _Note:_ `<name>` can be either server name or IP address. <br><br> _Type:_ string <br> _default:_ ```localhost:8080``` <br> _Example:_ ``foo.foobar.com:8080,bar.foobar.com:8080`` <br><br> Currently, the stress test framework only passes one server address to the client. So it is okay for the stress client to accept only one server address for now
`--test_cases`        |   List of test cases to call along with the relative weights in the following format: <br> `<testcase_1:w_1>,<testcase_2:w_2>...<testcase_n:w_n>`. <br>  The test cases names are those currently used by the interop client <br><br> _Type:_ string <br> _Example:_ `empty_unary:20,large_unary:10,empty_stream:70` <br><br> The stress client would then make `empty_unary` calls 20% of the time, `large_unary` calls 10% of the time and `empty_stream` calls 70% of the time. <br> _Note:_ The weights need not add up to 100
`--test_duration-secs`     | The length of time (in seconds) to run the test. Enter -1 if the test should run forever until forcefully terminated. <br> _Type:_ int <br> _default:_ -1
`--num_channels_per_server` |        Number of channels (i.e connections) to each server. <br> _Type:_ int <br> _default:_ 1 <br><br> _Note:_ Unfortunately, the term `channel` is used differently in `grpc-java`. In this context, this really means "number of connections to the server" 
`--num_stubs_per_channel `  |  Number of stubs per each connection to server.  <br> _Type:_ int <br> _default:_ 1
`--metrics_port`            | The port on which the stress client exposes the [QPS metrics](https://github.com/grpc/grpc/blob/master/src/proto/grpc/testing/metrics.proto). <br> _Type:_ int <br> _default:_ 8081 <br><br> _Note:_ This is mainly needed for the framework to know the health of stress test client.
