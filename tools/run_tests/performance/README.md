# Overview of performance test suite, with steps for manual runs:

For design of the tests, see
https://grpc.io/docs/guides/benchmarking.html.

## Pre-reqs for running these manually:
In general the benchmark workers and driver build scripts expect
[linux_performance_worker_init.sh](../../gce/linux_performance_worker_init.sh) to have been ran already.

### To run benchmarks locally:
* From the grpc repo root, start the
[run_performance_tests.py](../run_performance_tests.py) runner script.

### On remote machines, to start the driver and workers manually:
The [run_performance_test.py](../run_performance_tests.py) top-level runner script can also
be used with remote machines, but for e.g., profiling the server,
it might be useful to run workers manually.

1. You'll need a "driver" and separate "worker" machines.
For example, you might use one GCE "driver" machine and 3 other
GCE "worker" machines that are in the same zone.

2. Connect to each worker machine and start up a benchmark worker with a "driver_port".
  * For example, to start the grpc-go benchmark worker:
  [grpc-go worker main.go](https://github.com/grpc/grpc-go/blob/master/benchmark/worker/main.go) --driver_port <driver_port>

#### Comands to start workers in different languages:
 * Note that these commands are what the top-level
   [run_performance_test.py](../run_performance_tests.py) script uses to
   build and run different workers through the
   [build_performance.sh](./build_performance.sh) script and "run worker"
   scripts (such as the [run_worker_java.sh](./run_worker_java.sh)).

##### Running benchmark workers for C-core wrapped languages (C++, Python, C#, Node, Ruby):
   * These are more simple since they all live in the main grpc repo.

```
$ cd <grpc_repo_root>
$ tools/run_tests/performance/build_performance.sh
$ tools/run_tests/performance/run_worker_<language>.sh
```

   * Note that there is one "run_worker" script per language, e.g.,
     [run_worker_csharp.sh](./run_worker_csharp.sh) for c#.

##### Running benchmark workers for gRPC-Java:
   * You'll need the [grpc-java](https://github.com/grpc/grpc-java) repo.

```
$ cd <grpc-java-repo>
$ ./gradlew -PskipCodegen=true :grpc-benchmarks:installDist
$ benchmarks/build/install/grpc-benchmarks/bin/benchmark_worker --driver_port <driver_port>
```

##### Running benchmark workers for gRPC-Go:
   * You'll need the [grpc-go repo](https://github.com/grpc/grpc-go)

```
$ cd <grpc-go-repo>/benchmark/worker && go install
$ # if profiling, it might be helpful to turn off inlining by building with "-gcflags=-l"
$ $GOPATH/bin/worker --driver_port <driver_port>
```

#### Build the driver:
* Connect to the driver machine (if using a remote driver) and from the grpc repo root:
```
$ tools/run_tests/performance/build_performance.sh
```

#### Run the driver:
1. Get the 'scenario_json' relevant for the scenario to run. Note that "scenario
  json" configs are generated from [scenario_config.py](./scenario_config.py).
  The [driver](../../../test/cpp/qps/qps_json_driver.cc) takes a list of these configs as a json string of the form: `{scenario: <json_list_of_scenarios> }`
  in its `--scenarios_json` command argument.
  One quick way to get a valid json string to pass to the driver is by running
  the [run_performance_tests.py](./run_performance_tests.py) locally and copying the logged scenario json command arg.

2. From the grpc repo root:

* Set `QPS_WORKERS` environment variable to a comma separated list of worker
machines. Note that the driver will start the "benchmark server" on the first
entry in the list, and the rest will be told to run as clients against the
benchmark server.

Example running and profiling of go benchmark server:
```
$ export QPS_WORKERS=<host1>:<10000>,<host2>,10000,<host3>:10000
$ bins/opt/qps_json_driver --scenario_json='<scenario_json_scenario_config_string>'
```

### Example profiling commands

While running the benchmark, a profiler can be attached to the server.

Example to count syscalls in grpc-go server during a benchmark:
* Connect to server machine and run:
```
$ netstat -tulpn | grep <driver_port> # to get pid of worker
$ perf stat -p <worker_pid> -e syscalls:sys_enter_write # stop after test complete
```

Example memory profile of grpc-go server, with `go tools pprof`:
* After a run is done on the server, see its alloc profile with:
```
$ go tool pprof --text --alloc_space http://localhost:<pprof_port>/debug/heap
```
