Copyright 2016 Google Inc.

#Documentation

grift is integration of [Apache Thrift](https://github.com/apache/thrift.git) Serializer with GRPC.

This integration allows you to use grpc to send thrift messages in C++ and java.

By default grift uses Compact Protocol to serialize thrift messages.

#Installation

Before Installing thrift make sure to apply this [patch](grpc_plugins_generate.patch) to third_party/thrift.
Go to third_party/thrift and follow the [INSTALLATION](https://github.com/apache/thrift.git) instructions to
install thrift.