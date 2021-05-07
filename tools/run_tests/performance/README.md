# Overview of performance test suite, with steps for manual runs:

For design of the tests, see https://grpc.io/docs/guides/benchmarking.

For scripts related to the GKE-based performance test suite (in development),
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

- For example, to start the grpc-go benchmark worker:
  [grpc-go worker main.go](https://github.com/grpc/grpc-go/blob/master/benchmark/worker/main.go)
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
written in multipart YAML format, either to a file or to stdout. Each
configuration contains a single embedded scenario.

The LoadTest configurations are generated from a template. Any configuration can
be used as a template, as long as it contains the languages required by the set
of scenarios we intend to run (for instance, if we are generating configurations
to run go scenarios, the template must contain a go client and a go server; if
we are generating configurations for cross-language scenarios that need a go
client and a C++ server, the template must also contain a C++ server; and the
same for all other languages).

The LoadTests specified in the script output all have unique names and can be
run by applying the test to a cluster running the LoadTest controller with
`kubectl apply`:

```
$ kubectl apply -f loadtest_config.yaml
```

A basic template for generating tests in various languages can be found here:
[loadtest_template_basic_all_languages.yaml](./templates/loadtest_template_basic_all_languages.yaml).
The following example generates configurations for C# and Java tests using this
template, including tests against C++ clients and servers, and running each test
twice:

```
$ ./tools/run_tests/performance/loadtest_config.py -l go -l java \
    -t ./tools/run_tests/performance/templates/loadtest_template_basic_all_languages.yaml \
    -s client_pool=workers-8core -s server_pool=workers-8core \
    -s big_query_table=grpc-testing.e2e_benchmarks.experimental_results \
    -s timeout_seconds=3600 --category=scalable \
    -d --allow_client_language=c++ --allow_server_language=c++ \
    --runs_per_test=2 -o ./loadtest.yaml
```

The script `loadtest_config.py` takes the following options:

- `-l`, `--language`<br> Language to benchmark. May be repeated.
- `-t`, `--template`<br> Template file. A template is a configuration file that
  may contain multiple client and server configuration, and may also include
  substitution keys.
- `p`, `--prefix`<br> Test names consist of a prefix_joined with a uuid with a
  dash. Test names are stored in `metadata.name`. The prefix is also added as
  the `prefix` label in `metadata.labels`. The prefix defaults to the user name
  if not set.
- `-u`, `--uniquifier_element`<br> Uniquifier elements may be passed to the test
  to make the test name unique. This option may be repeated to add multiple
  elements. The uniquifier elements (plus a date string and a run index, if
  applicable) are joined with a dash to form a _uniquifier_. The test name uuid
  is derived from the scenario name and the uniquifier. The uniquifier is also
  added as the `uniquifier` annotation in `metadata.annotations`.
- `-d`<br> This option is a shorthand for the addition of a date string as a
  uniquifier element.
- `-a`, `--annotation`<br> Metadata annotation to be stored in
  `metadata.annotations`, in the form key=value. May be repeated.
- `-r`, `--regex`<br> Regex to select scenarios to run. Each scenario is
  embedded in a LoadTest configuration containing a client and server of the
  language(s) required for the test. Defaults to `.*`, i.e., select all
  scenarios.
- `--category`<br> Select scenarios of a specified _category_, or of all
  categories. Defaults to `all`. Continuous runs typically run tests in the
  `scalable` category.
- `--allow_client_language`<br> Allows cross-language scenarios where the client
  is of a specified language, different from the scenario language. This is
  typically `c++`. This flag may be repeated.
- `--allow_server_language`<br> Allows cross-language scenarios where the server
  is of a specified language, different from the scenario language. This is
  typically `node` or `c++`. This flag may be repeated.
- `--runs_per_test`<br> This option specifies that each test should be repeated
  `n` times, where `n` is the value of the flag. If `n` > 1, the index of each
  test run is added as a uniquifier element for that run.
- `-o`, `--output`<br> Output file name. The LoadTest configurations are added
  to this file, in multipart YAML format. Output is streamed to `sys.stdout` if
  not set.

The script adds labels and annotations to the metadata of each LoadTest
configuration:

The following labels are added to `metadata.labels`:

- `language`<br> The language of the LoadTest scenario.
- `prefix`<br> The prefix used in `metadata.name`.

The following annotations are added to `metadata.annotations`:

- `scenario`<br> The name of the LoadTest scenario.
- `uniquifier`<br> The uniquifier used to generate the LoadTest name, including
  the run index if applicable.

[Labels](https://kubernetes.io/docs/concepts/overview/working-with-objects/labels/)
can be used in selectors in resource queries. Adding the prefix, in particular,
allows the user (or an automation script) to select the resources started from a
given run of the config generator.

[Annotations](https://kubernetes.io/docs/concepts/overview/working-with-objects/annotations/)
contain additional information that is available to the user (or an automation
script) but is not indexed and cannot be used to select objects. Scenario name
and uniquifier are added to provide the elements of the LoadTest name uuid in
human-readable form. Additional annotations may be added later for automation.

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

The script [loadtest_template.py](./loadtest_template.py) generates a load test
configuration template from a set of load test configurations. The source files
may be load test configurations or load test configuration templates. The
generated template supports all languages supported in any of the input
configurations or templates.

The example template in
[loadtest_template_basic_template_all_languages.yaml](./templates/loadtest_template_basic_all_languages.yaml)
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

The script `loadtest_template.py` takes the following options:

- `-i`, `--inputs`<br> Space-separated list of the names of input files
  containing LoadTest configurations. May be repeated.
- `-o`, `--output`<br> Output file name. Outputs to `sys.stdout` if not set.
- `--inject_client_pool`<br> If this option is set, the pool attribute of all
  clients in `spec.clients` is set to `${client_pool}`, for later substitution.
- `--inject_server_pool`<br> If this option is set, the pool attribute of all
  servers in `spec.servers` is set to `${server_pool}`, for later substitution.
- `--inject_big_query_table`<br> If this option is set,
  spec.results.bigQueryTable is set to `${big_query_table}`.
- `--inject_timeout_seconds`<br> If this option is set, `spec.timeoutSeconds` is
  set to `${timeout_seconds}`.
- `--inject_ttl_seconds`<br> If this option is set, `spec.ttlSeconds` is set to
  `${ttl_seconds}`.
- `-n`, `--name`<br> Name to be set in `metadata.name`.
- `-a`, `--annotation`<br> Metadata annotation to be stored in
  `metadata.annotations`, in the form key=value. May be repeated.

The four options that inject substitution keys are the most useful for template
reuse. When running tests on different node pools, it becomes necessary to set
the pool, and usually also to store the data on a different table. When running
as part of a larger collection of tests, it may also be necessary to adjust test
timeout and time-to-live, to ensure that all tests have time to complete.

The template name is replaced again by `loadtest_config.py`, and so is set only
as a human-readable memo.

Annotations, on the other hand, are passed on to the test configurations, and
may be set to values or to substitution keys in themselves, allowing future
automation scripts to process the tests generated from these configurations in
different ways.
