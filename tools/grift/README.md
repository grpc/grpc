Copyright 2016 Google Inc.

#Documentation

grift is integration of [Apache Thrift](https://github.com/apache/thrift.git) Serializer with gRPC.

This integration allows you to use grpc to send thrift messages in C++ and java.

grift uses Compact Protocol to serialize thrift messages. 

##generating grpc plugins for thrift services

###CPP
```sh
 $ thrift --gen cpp <thrift-file>
```

###JAVA
```sh
 $ thrift --gen java <thrift-file>
```

#Installation

Before Installing thrift make sure to apply this [patch](grpc_plugins_generator.patch) to third_party/thrift.
Go to third_party/thrift and follow the [INSTALLATION](https://github.com/apache/thrift.git) instructions to install thrift with commit id bcad91771b7f0bff28a1cac1981d7ef2b9bcef3c.