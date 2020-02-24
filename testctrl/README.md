# TestCtrl

TestCtrl, written and pronounced "test control" in a Kubernetes fashion,
provides a gRPC server and drivers that queue tests, schedule builds of
appropriate containers, run them on a GKE cluster and report the results.

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
