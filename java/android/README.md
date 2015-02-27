gRPC Hello World Tutorial (Android Java)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto](https://github.com/grpc/grpc-common/blob/master/protos/helloworld.proto). 

PREREQUISITES
-------------
- [Java gRPC](https://github.com/grpc/grpc-java)

- [Android Tutorial](https://developer.android.com/training/basics/firstapp/index.html) if you're new to Android development

- We only have Android gRPC client in this example. Please follow examples in other languages to build and run a gRPC server.

INSTALL
-------
**1 Clone the gRPC Java git repo**
```sh
$ git clone https://github.com/grpc/grpc-java
```

**2 Install gRPC Java, as described in [How to Build](https://github.com/grpc/grpc-java#how-to-build)**
```sh
$ # from this dir
$ cd grpc-java
$ # follow the instructions in 'How to Build'
```

**3 Prepare the app**
- Clone this git repo
```sh
$ git clone https://github.com/grpc/grpc-common

```

**4 Install the app**
```sh
$ cd grpc-common
$ ./gradlew installDebug
```
