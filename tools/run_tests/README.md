# Overview

This directory contains scripts that facilitate building and running tests. We are using python scripts as entrypoint for our
tests because that gives us the opportunity to run tests using the same commandline regardless of the platform you are using.

# Unit tests (run_tests.py)

Builds gRPC in given language and runs unit tests. Use `tools/run_tests/run_tests.py --help` for more help.

###### Example
`tools/run_tests/run_tests.py -l csharp -c dbg`

###### Useful options (among many others)
- `--use_docker` Builds a docker container containing all the prerequisites for given language and runs the tests under that container.
- `--build_only` Only build, do not run the tests.

Note: If you get an error such as `ImportError: No module named httplib2`, then you may be missing some Python modules. Install the module listed in the error and try again. 

Note: some tests may be flaky. Check the "Issues" tab for known flakes and other issues.

The full suite of unit tests will take many minutes to run.

# Interop tests (run_interop_tests.py)

Runs tests for cross-platform/cross-language interoperability. For more details, see [Interop tests descriptions](/doc/interop-test-descriptions.md)
The script is also capable of running interop tests for grpc-java and grpc-go, using sources checked out alongside the ones of the grpc repository.

###### Example
`tools/run_tests/run_interop_tests.py -l csharp -s c++ --use_docker` (run interop tests with C# client and C++ server)

Note: if you see an error like `no space left on device` when running the
interop tests using Docker, make sure that Docker is building the image files in
a location with sufficient disk space.

# Performance benchmarks (run_performance_tests.py)

Runs predefined benchmark scenarios for given languages. Besides the simple configuration of running all the scenarios locally,
the script also supports orchestrating test runs with client and server running on different machines and uploading the results
to BigQuery.

###### Example
`tools/run_tests/run_performance_tests.py -l c++ node`

###### Useful options
- `--regex` use regex to select particular scenarios to run.

# Artifacts & Packages (task_runner.py)

A generalized framework for running predefined tasks based on their labels. We use this to building binary artifacts & distrib packages and testing them)

###### Example
`tools/run_tests/task_runner.py -f python artifact linux x64` (build tasks with labels `python`, `artifact`, `linux`, and `x64`)

