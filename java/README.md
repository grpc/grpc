gRPC in 3 minutes (Java)
========================

PREREQUISITES
-------------

- [Java 8](http://docs.oracle.com/javase/8/docs/technotes/guides/install/install_overview.html)

- [Maven 3.2 or later](http://maven.apache.org/download.cgi).
  - this is needed to install Netty, a dependency of gRPC

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

TRY IT!
-------

Our [Gradle build file](https://github.com/grpc/grpc-java/blob/master/examples/build.gradle) simplifies building and running the examples.

You can build and run the Hello World server used in [Getting started](https://github.com/grpc/grpc-common) from the `grpc-java` root folder with:

```sh
$ ./gradlew :grpc-examples:helloWorldServer
```

and in another terminal window confirm that it receives a message.

```sh
$  ./gradlew :grpc-examples:helloWorldClient
```

TUTORIAL
--------

You can find a more detailed tutorial in [gRPC Basics: Java](https://github.com/grpc/grpc-common/blob/master/java/javatutorial.md).
