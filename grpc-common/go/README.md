gRPC in 3 minutes (Go)
======================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto](https://github.com/grpc/grpc-common/blob/master/protos/helloworld.proto). 

PREREQUISITES
-------------

- This requires Go 1.4
- Requires that [GOPATH is set](https://golang.org/doc/code.html#GOPATH)
```sh
$ go help gopath
$ # ensure the PATH contains $GOPATH/bin
$ export PATH=$PATH:$GOPATH/bin
```

INSTALL
-------

```sh
$ go get -u github.com/grpc/grpc-common/go/greeter_client
$ go get -u github.com/grpc/grpc-common/go/greeter_server
```

TRY IT!
-------

- Run the server
```sh
$ greeter_server &
```

- Run the client
```sh
$ greeter_client
```

OPTIONAL - Rebuilding the generated code
----------------------------------------

1 First [install protoc](https://github.com/google/protobuf/blob/master/INSTALL.txt)
  - For now, this needs to be installed from source
  - This is will change once proto3 is officially released

2 Install the protoc Go plugin.
```sh
$ go get -a github.com/golang/protobuf/protoc-gen-go
$
$ # from this dir; invoke protoc
$ protoc -I ../protos ../protos/helloworld.proto --go_out=plugins=grpc:helloworld
```
