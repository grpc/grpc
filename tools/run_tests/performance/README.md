# Overview of performance test suite, with steps for manual runs:

For design of the tests, see https://grpc.io/docs/guides/benchmarking.

For scripts related ot the GKE-based performance test suite (in development).
see [gRPC OSS benchmarks](#grpc-oss-benchmarks).

## Pre-reqs for running these manually:

In general the benchmark workers and driver build scripts expect
[linux_performance_worker_init.sh](../../gce/linux_performance_worker_init.sh)
to have been ran already.

### To run benchmarks locally:

- From the grpc repo root, start the
  [run_performance_tests.py](../run_performance_tests.py) runner script.

### On remote machines, to start the driver and workers manually:

The [run_performance_test.py](../run_performance_tests.py) top-level runner
script can also be used with remote machines, but for e.g., profiling the
server, it might be useful to run workers manually.

1. You'll need a "driver" and separate "worker" machines. For example, you might
   use one GCE "driver" machine and 3 other GCE "worker" machines that are in
   the same zone.

2. Connect to each worker machine and start up a benchmark worker with a
   "driver_port".

- For example, to start the grpc-go benchmark worker: [grpc-go worker
  main.go](https://github.com/grpc/grpc-go/blob/master/benchmark/worker/main.go)
  --driver_port <driver_port>

#### Commands to start workers in different languages:

- Note that these commands are what the top-level
  [run_performance_test.py](../run_performance_tests.py) script uses to build
  and run different workers through the
  [build_performance.sh](./build_performance.sh) script and "run worker" scripts
  (such as the [run_worker_java.sh](./run_worker_java.sh)).

##### Running benchmark workers for C-core wrapped languages (C++, Python, C#, Node, Ruby):

- These are more simple since they all live in the main grpc repo.

```
$ cd <grpc_repo_root>
$ tools/run_tests/performance/build_performance.sh
$ tools/run_tests/performance/run_worker_<language>.sh
```

- Note that there is one "run_worker" script per language, e.g.,
  [run_worker_csharp.sh](./run_worker_csharp.sh) for c#.

##### Running benchmark workers for gRPC-Java:

- You'll need the [grpc-java](https://github.com/grpc/grpc-java) repo.

```
$ cd <grpc-java-repo>
$ ./gradlew -PskipCodegen=true -PskipAndroid=true :grpc-benchmarks:installDist
$ benchmarks/build/install/grpc-benchmarks/bin/benchmark_worker --driver_port <driver_port>
```

##### Running benchmark workers for gRPC-Go:

- You'll need the [grpc-go repo](https://github.com/grpc/grpc-go)

```
$ cd <grpc-go-repo>/benchmark/worker && go install
$ # if profiling, it might be helpful to turn off inlining by building with "-gcflags=-l"
$ $GOPATH/bin/worker --driver_port <driver_port>
```

#### Build the driver:

- Connect to the driver machine (if using a remote driver) and from the grpc
  repo root:

```
$ tools/run_tests/performance/build_performance.sh
```

#### Run the driver:

1. Get the 'scenario_json' relevant for the scenario to run. Note that "scenario
   json" configs are generated from [scenario_config.py](./scenario_config.py).
   The [driver](../../../test/cpp/qps/qps_json_driver.cc) takes a list of these
   configs as a json string of the form: `{scenario: <json_list_of_scenarios> }`
   in its `--scenarios_json` command argument. One quick way to get a valid json
   string to pass to the driver is by running the
   [run_performance_tests.py](./run_performance_tests.py) locally and copying
   the logged scenario json command arg.

2. From the grpc repo root:

- Set `QPS_WORKERS` environment variable to a comma separated list of worker
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

- Connect to server machine and run:

```
$ netstat -tulpn | grep <driver_port> # to get pid of worker
$ perf stat -p <worker_pid> -e syscalls:sys_enter_write # stop after test complete
```

Example memory profile of grpc-go server, with `go tools pprof`:

- After a run is done on the server, see its alloc profile with:

```
$ go tool pprof --text --alloc_space http://localhost:<pprof_port>/debug/heap
```

### Configuration environment variables:

- QPS_WORKER_CHANNEL_CONNECT_TIMEOUT

  Consuming process: qps_worker

  Type: integer (number of seconds)

  This can be used to configure the amount of time that benchmark clients wait
  for channels to the benchmark server to become ready. This is useful in
  certain benchmark environments in which the server can take a long time to
  become ready. Note: if setting this to a high value, then the scenario config
  under test should probably also have a large "warmup_seconds".

- QPS_WORKERS

  Consuming process: qps_json_driver

  Type: comma separated list of host:port

  Set this to a comma separated list of QPS worker processes/machines. Each
  scenario in a scenario config has specifies a certain number of servers,
  `num_servers`, and the driver will start "benchmark servers"'s on the first
  `num_server` `host:port` pairs in the comma separated list. The rest will be
  told to run as clients against the benchmark server.

## gRPC OSS benchmarks

The scripts in this section generate LoadTest configurations for the GKE-based
gRPC OSS benchmarks framework. This framework is stored in a separate
repository, [grpc/test-infra](https://github.com/grpc/test-infra).

### Generating scenarios

The benchmarks framework uses the same test scenarios as the legacy one. These
script [scenario_config_exporter.py](./scenario_config_exporter.py) can be used
to export these scenarios to files, and also to count and analyze existing
scenarios.

The language(s) and category of the scenarios are of particular importance to
the tests. Continuous runs will typically run tests in the `scalable` category.

The following example counts scenarios in the `scalable` category:

```
$ ./tools/run_tests/performance/scenario_config_exporter.py --count_scenarios --category=scalable
Scenario count for all languages (category: scalable):
Count  Language         Client   Server   Categories
   77  c++                                scalable
   19  python_asyncio                     scalable
   16  java                               scalable
   12  go                                 scalable
   12  node                      node     scalable
   12  node_purejs               node     scalable
    9  csharp                             scalable
    7  python                             scalable
    5  ruby                               scalable
    4  csharp                    c++      scalable
    4  php7                      c++      scalable
    4  php7_protobuf_c           c++      scalable
    3  python_asyncio            c++      scalable
    2  ruby                      c++      scalable
    2  python                    c++      scalable
    1  csharp           c++               scalable

  189  total scenarios (category: scalable)
```

Client and server languages are only set for cross-language scenarios, where the
client or server language do not match the scenario language.

### Generating load test configurations

The benchmarks framework uses LoadTest resources configured by YAML files. Each
LoadTest resource specifies a driver, a server, and one or more clients to run
the test. Each test runs one scenario. The scenario configuration is embedded in
the LoadTest configuration. Example configurations for various languages can be
found here:

https://github.com/grpc/test-infra/tree/master/config/samples

The script [loadtest_config.py](./loadtest_config.py) generates LoadTest
configurations for tests running a set of scenarios. The configurations are
written in multipart YAML format, either to a file or to stdout.

The LoadTest configurations are generated from a template. Any configuration can
be used as a template, as long as it supports the languages that need to be
supported in the script output.

The LoadTests specified in the script output all have unique names and can be
run by applying the test to a cluster running the LoadTest controller with
`kubectl apply`:

```
$ kubectl apply -f loadtest_config.yaml
```

<!-- TODO(paulosjca): add more details on scripts and running tests. -->

### Concatenating load test configurations

The LoadTest configuration generator can process multiple languages at a time,
assuming that they are supported by the template. The convenience script
[loadtest_concat_yaml.py](./loadtest_concat_yaml.py) is provided to concatenate
several YAML files into one, so configurations generated by multiple generator
invocations can be concatenated into one and run with a single command. The
script can be invoked as follows:

```
$ loadtest_concat_yaml.py -i infile1.yaml infile2.yaml -o outfile.yaml
```

### Generating configuration templates

The script [loadtest_template.py][./loadtest_template.py] generates a load test
configuration template from a set of load test configurations. The generated
template supports all languages supported in any of the input configurations.

The example template in [basic_template.yaml][./templates/basic_template.yaml]
was generated from the example configurations in
[grpc/test-infra](https://github.com/grpc/test-infra) by the following command:

```
$ ./tools/run_tests/performance/loadtest_template.py \
    -i ../test-infra/config/samples/*.yaml \
    --inject_client_pool --inject_server_pool --inject_big_query_table \
    --inject_timeout_seconds \
    -o ./tools/run_tests/performance/templates/loadtest_template_basic_all_languages.yaml \
    --name basic_all_languages
```

Configurations for C# and Java tests, including tests against C++ clients and
servers, running each test twice, can be generated from this template as
follows:

```shell
$ ./tools/run_tests/performance/loadtest_config.py -l go -l java \
    -t ./tools/run_tests/performance/templates/loadtest_template_basic_all_languages.yaml \
    -s client_pool=workers-8core -s server_pool=workers-8core \
    -s big_query_table=grpc-testing.e2e_benchmarks.experimental_results \
    -s timeout_seconds=3600 --category=scalable \
    -d --allow_client_language=c++ --allow_server_language=c++ \
    --runs_per_test=2 -o ./loadtest.yaml
```
