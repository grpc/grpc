gRPC in 3 minutes (Java)
========================

PREREQUISITES
-------------

- [Java 8](http://docs.oracle.com/javase/8/docs/technotes/guides/install/install_overview.html)

- [Maven 2.3](http://maven.apache.org/users/index.html).
  - this is needed to install Netty5, a dependency of gRPC, and to build this sample

- [Latest version of google-protobuf](https://github.com/google/protobuf/tree/master/java)
  - to generate java code from proto files
  - to install the base Java proto3 library


INSTALL
-------

1 Clone the gRPC Java git repo
```sh
$ cd <path/to/your/working_dir>
$ git clone https://github.com/grpc/grpc-java
```

2 Install gRPC Java, as described in [How to Build](https://github.com/grpc/grpc-java#how-to-build)
```sh
$ # from this dir
$ cd grpc-java
$ # follow the instructions in 'How to Build'
```

3 Clone this repo, if you've not already done so.
```sh
$ cd <path/to/your/working_dir>
$ git clone https://github.com/grpc/grpc-common
$ cd grpc-common/java  # switch to this directory
```

4 Build the samples
```sh
$ # from this directory
$ mvn package
```

TRY IT!
-------

- Run the server
```sh
$ # from this directory
$ ./run_greeter_server.sh &
```

- Run the client
```sh
$ # from this directory
$ ./run_greeter_client.sh
```
