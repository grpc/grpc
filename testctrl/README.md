# TestCtrl

TestCtrl, written and pronounced "test control" in a Kubernetes fashion,
provides an interface for scheduling and running benchmarks on Kubernetes.

## Overview

The TestCtrl project is composed of three major components:

The **server** which implements two gRPC services: **`TestSessions`** and
**`google.longrunning.Operations`**. This services process incoming user
requests and status updates for the tests.

A **store** is created with information about test sessions. It contains the
configuration for the tests components, which TestCtrl knows nothing about. In
addition, it stores metadata and the current status of the test sessions.

One **controller** is created which operates a work queue to limit the number of
test sessions in progress. It spawns workers that process one test session at a
time, provisioning and monitoring resources with Kubernetes. It continually
updates the store with the status of the test.

### Life of a Test Session

To run benchmarks, a user calls the **`TestSessions`** service with the tests
they would like to run and the container images that these tests require. This
service saves information about the tests in a [store], which assigns the test
session a globally unique name. This name is stored in the work queue, awaiting
an available worker. The server replies with the name in a
**`[google.longrunning.Operation]`** protobuf.

When a worker becomes available, it fetches the next job name off of the work
queue. Then, it uses this name to find the appropriate configuration in the
store. The worker communicates with the Kubernetes API, monitoring the status of
the pods that the test requires. It updates the store with significant events
and timestamps.

If a user desires to see progress of a test, they make a request with the name
to TestCtrl's implementation of the **`google.longrunning.Operations`** service.
This service queries the store to learn about the status of the test and where
to locate test results or error logs upon completion.

## Quickstart

To start the service without containers on your local machine, run:

    $ make svc
    $ bin/svc

You can force the service to use a different port with the flag `-port`.

When the service is started, it looks for an `$APP_ENV` environment variable.

If this variable is set to `"production"`, it authenticates with the Kubernetes
API using in-cluster authentication.

Otherwise, the environment is assumed to be `"development"`. In this case, the
service will fail if the `$KUBE_CONFIG_FILE` does not contain the absolute path
of a kubernetes config for authentication.

## Development

To build or contribute to this project, your system must be properly configured
with a version of Go that provides module support. Once navigated to the
testctrl directory, running `make deps` should install all the dependencies
specified in the [go.mod](go.mod) file.

### Building Protobufs

The protobufs are defined in the [proto/](proto) directory.  Since they do not
rely on a build system, they require an installation of the protobuf compiler
and the go plugins.

To install the go plugin, navigate outside of the project directory and run:

    $ go get -u -v github.com/golang/protobuf/protoc-gen-go

This binary will need to be added to your `$PATH` in order for a successful
build.  It will be available wherever your system places go binaries.

Once protoc is available, all protos can be built using:

    $ make proto

### Inspecting the Service with grpc\_cli

In order to inspect the gRPC service and make remote procedure calls on the
command line, gRPC releases the [grpc\_cli]. To build from source,
navigate to the root of [grpc/grpc] and run:

    $ make grpc_cli

The tool will become available as a binary under the *bins/opt/* directory.  For
easy access, it may be helpful to create a shell alias that expands to it.

### Formatting and Linting

Finally, before submitting any code to GitHub, run it through `gofmt` to ensure
that it follows the external Go style guides and [golint] to point out any lint
errors.  You can do this by running:

    $ make fmt && make lint

[golint]: https://github.com/golang/lint
[grpc\_cli]: https://github.com/grpc/grpc/blob/master/doc/command_line_tool.md

