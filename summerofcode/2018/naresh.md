# Project overview

## Title

Enable Building of gRPC Python with Bazel

## Overview

gRPC Python currently has a constellation of scripts written to build the
project, but it has a lot of limitations in terms of speed and maintainability.
[Bazel](https://bazel.build/) is the open-sourced variant of Google's internal
system, Blaze, which is an ideal replacement for building such projects in a
fast and declarative fashion. But Bazel in itself is still in active
development, especially in terms of Python (amongst a few other languages).

The project aimed to fill this gap and build gRPC Python with Bazel.

[Project page](https://summerofcode.withgoogle.com/projects/#6482576244473856)

[Link to proposal](https://storage.googleapis.com/summerofcode-prod.appspot.com/gsoc/core_project/doc/5316764725411840_1522049732_Naresh_Ramesh_-_GSoC_proposal.pdf)

## Thoughts and challenges

### State of Bazel for Python

Although previously speculated, the project didn't require any contributions
directly to [bazelbuild/bazel](https://github.com/bazelbuild/bazel). The Bazel
rules for Python are currently being separated out into their own repo at
[bazelbuild/rules_python](https://github.com/bazelbuild/rules_python/).

Bazel is [still very much in active development for
Python](https://groups.google.com/forum/#!topic/bazel-sig-python/iQjV9sfSufw)
though. There's still challenges when it comes to building for Python 2 vs 3.
Using pip packages is still in experimental. Bazel Python support is currently
distributed across these two repositories and is yet to begin migration to one
place (which will be
[bazelbuild/rules_python](https://github.com/bazelbuild/rules_python/)).

Bazel's roadmap for Python is publicly available [here as a Google
doc](https://docs.google.com/document/d/1A6J3j3y1SQ0HliS86_mZBnB5UeBe7vExWL2Ryd_EONI/edit).

### Cross collaboration between projects

Cross contribution surprisingly came up because of building protobuf sources
for Python, which is still not natively supported by Bazel. An existing
repository, [pubref/rules_protobuf](https://github.com/pubref/rules_protobuf),
which was maintained by an independent maintainer (i.e. not a part of Bazel)
helped solve this problem, but had [one major blocking
issue](https://github.com/pubref/rules_protobuf/issues/233) and could not be
resolved at the source. But [a solution to the
issue](https://github.com/pubref/rules_protobuf/pull/196) was proposed by user
dududko, which was not merged because of failing golang tests but worked well
for Python. Hence, a fork of this repo was made and is to be used with gRPC
until the solution can be merged back at the source.

### Building Cython code

Building Cython code is still not supported by Bazel, but the team at
[cython/cython](https://github.com/cython/cython) have added support for Bazel
on their side. The way it works is by including Cython as a third-party Bazel
dependency and using custom Bazel rules for building our Cython code using the
binary within the dependency.

### Packaging Python code using Bazel

pip and PyPI still remain the de-facto standard for distributing Python
packages. Although Bazel is pretty versatile and is amazing for it's
reproducible and incremental build capabilities, these can only be still used
by the contributors and developers for building and testing the gRPC code. But
there's no way yet to build Python packages for distribution.

### Building gRPC Python with Bazel on Kokoro (internal CI)

Integration with the internal CI was one of the areas that highlighted how
simple Bazel can be to use. gRPC was already using a dockerized Bazel setup to
build some of it's core code (but not as the primary build setup). Adding a new
job on the internal CI ended up being as simple as creating a new shell script
to install the required dependencies (which were python-dev and Bazel) and a
new configuration file which pointed to the subdirectiory (src/python) under
which to look for targets and run the tests accordingly.

### Handling imports in Python code

When writing Python packages, imports in nested modules are typically made
relative to the package root. But because of the way Bazel works, these paths
wouldn't make sense from the Workspace root. So, the folks at Bazel have added
a nifty `imports` parameter to all the Python rules which lets us specify for
each target, which path to consider as the root. This parameter allows for
relative paths like `imports = ["../",]`.

### Fetching Python headers for Cython code to use

Cython code makes use of `Python.h`, which pulls in the Python API for C
extension modules to use, but it's location depending on the Python version and
operating system the code is building on. To make this easier, the folks at
Tensorflow wrote [repository rules for Python
autoconfiguration](https://github.com/tensorflow/tensorflow/tree/e447ae4759317156d31a9421290716f0ffbffcd8/third_party/py).
This has been [adapted with some some
modifications](https://github.com/grpc/grpc/pull/15992) for use in gRPC Python
as well.

## How to use

All the Bazel tests for gRPC Python can be run using a single command:

```bash
bazel test --spawn_strategy=standalone --genrule_strategy=standalone //src/python/...
```

If any specific test is to be run, like say `LoggingPoolTest` (which is present
in
`src/python/grpcio_tests/tests/unit/framework/foundation/_logging_pool_test.py`),
the command to run would be:

```bash
bazel test --spawn_strategy=standalone --genrule_strategy=standalone //src/python/grpcio_tests/tests/unit/framework/foundation:logging_pool_test
```

where, `logging_pool_test` is the name of the Bazel target for this test.

Similarly, to run a particular method, use:

```bash
bazel test --spawn_strategy=standalone --genrule_strategy=standalone //src/python/grpcio_tests/tests/unit/_rpc_test --test_arg=RPCTest.testUnrecognizedMethod
```

## Useful Bazel flags

- Use `bazel build` with a `-s` flag to see the logs being printed out to
    standard output while building. 
- Similarly, use `bazel test` with a `--test_output=streamed` to see the
    test logs while testing. Something to know while using this flag is that all
    tests will be run locally, without sharding, one at a time.

## Contributions

### Related to the project

- [435c6f8](https://github.com/grpc/grpc/commit/435c6f8d1e53783ec049b3482445813afd8bc514)
    Update grpc_gevent cython files to include .pxi
- [74426fd](https://github.com/grpc/grpc/commit/74426fd2164c51d6754732ebe372133c19ba718c)
    Add gevent_util.h to grpc_base_c Bazel target
- [b6518af](https://github.com/grpc/grpc/commit/b6518afdd610f0115b42aee1ffc71520c6b0d6b1)
    Upgrade Bazel to 0.15.0
- [ebcf04d](https://github.com/grpc/grpc/commit/ebcf04d075333c42979536c5dd2091d363f67e5a)
    Kokoro setup for building gRPC Python with Bazel
- [3af1aaa](https://github.com/grpc/grpc/commit/3af1aaadabf49bc6274711a11f81627c0f351a9a)
    Basic setup to build gRPC Python with Bazel
- [11f199e](https://github.com/grpc/grpc/commit/11f199e34dc416a2bd8b56391b242a867bedade4)
    Workspace changes to build gRPC Python with Bazel
- [848fd9d](https://github.com/grpc/grpc/commit/848fd9d75f6df10f00e8328ff052c0237b3002ab)
    Minimal Bazel BUILD files for grpcio Python

### Other contibutions

- [89ce16b](https://github.com/grpc/grpc/commit/89ce16b6daaad4caeb1c9ba670c6c4b62ea1a93c)
    Update Dockerfiles for python artifacts to use latest git version
- [32f7c48](https://github.com/grpc/grpc/commit/32f7c48dad71cac7af652bf994ab1dde3ddb0607)
    Revert removals from python artifact dockerfiles
- [712eb9f](https://github.com/grpc/grpc/commit/712eb9ff91cde66af94e8381ec01ad512ed6d03c)
    Make logging after success in jobset more apparent
- [c6e4372](https://github.com/grpc/grpc/commit/c6e4372f8a93bb0eb996b5f202465785422290f2)
    Create README for gRPC Python reflection package
- [2e113ca](https://github.com/grpc/grpc/commit/2e113ca6b2cc31aa8a9687d40ee1bd759381654f)
    Update logging in Python to use module-level logger

### Pending PRs

- BUILD files for all tests in
    [tests.json](https://github.com/ghostwriternr/grpc/blob/70c8a58b2918a5369905e5a203d7ce7897b6207e/src/python/grpcio_tests/tests/tests.json).
- BUILD files for gRPC testing, gRPC health checking, gRPC reflection.
- (Yet to complete) BUILD files for grpcio_tools. One test depends on this.

## Known issues

- [grpc/grpc #16336](https://github.com/grpc/grpc/issues/16336) RuntimeError
    for `_reconnect_test` Python unit test with Bazel
- Some tests in Bazel pass despite throwing an exception. Example:
    `testAbortedStreamStream` in
    `src/python/grpcio_tests/tests/unit/_metadata_code_details_test.py`.
- [#14557](https://github.com/grpc/grpc/pull/14557) introduced a minor bug
    where the module level loggers don't initialize a default logging handler.
- Sanity test doesn't make sense in the context of Bazel, and thus fails.
- There are some issues with Python2 vs Python3. Specifically,
  - On some machines, “cygrpc.so: undefined symbol: _Py_FalseStruct” error
    shows up. This is because of incorrect Python version being used to build
    Cython.
  - Some external packages like enum34 throw errors when used with Python 3 and
    some extra packages are currently installed as Python version in current
    build scripts. For now, the extra packages are added to a
    `requirements.bazel.txt` file in the repository root.
