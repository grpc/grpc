# TestCtrl

TestCtrl, written and pronounced "test control" in a Kubernetes tribute,
provides a gRPC server and drivers that queue tests, schedule builds of
appropriate containers, run them on a GKE cluster and report the results.

## Quickstart

The service, defined in [proto/testctrl.proto](proto/testctrl.proto), can be
started locally or in a Docker container.

To start the service, either build and run the container image produced by the
[Dockerfile](Dockerfile) or build and start the service in your local shell:

    $ go build ./...
    $ ./testctrl

You can force the service to use a different port with the flag `-port` and an
array of other options specified in the [main.go](main.go) file.

If access to the service through the [grpc\_cli] tool is required, reflection
must be enabled at startup using the flag `-enableReflection`.  For this to
work, you must have a freshly-built version of the grpc\_cli tool.  The
[Development](#development) section describes how to build it.

## Development

To build or contribute to this project, your system must be properly configured
with a version of Go that provides module support. Once navigated to the
testctrl directory, running `go build ./...` should install all the
dependencies specified in the [go.mod](go.mod) file.

### Building Protobufs

The protobufs are defined in the [proto/](proto) directory.  Since they do not
rely on a build system, they require an installation of the protobuf compiler
and the go plugins.

To install the go plugin, navigate outside of the project directory and run:

    $ go get -u -v github.com/golang/protobuf/protoc-gen-go

The binary will need to be added to your `$PATH` in order for a successful
build.  It will be installed in your `$GOPATH` if set.  Likely, this will be
`~/go` if unset.  You can add installed go binaries to your path with:

    $ export PATH="$PATH:<GO_PATH>/bin"

where `<GO_PATH>` is your set `$GOPATH` or likely `~/go`.

When this is accessible, you can build the protobufs by navigating into the
[proto](proto) directory and running:

    $ protoc -I ./ *.proto --go_out=plugins=grpc:.

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

    $ gofmt -w <LIST_OF_GO_FILES_CHANGED>
    $ golint ./...

It may be easier to configure an editor with a Go plugin. This will likely run
`gofmt` and [golint] automatically.  There are plugins for many editors, but
we need to pay special attention to their license and telemetry features in
order to not violate security guidelines.

[golint]: https://github.com/golang/lint
[grpc\_cli]: https://github.com/grpc/grpc/blob/master/doc/command_line_tool.md

