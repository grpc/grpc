#!/bin/bash

# Generate without async suffix
protoc --csharp_out=./Server/ --grpc_out=./Server/ --plugin=protoc-gen-grpc=grpc_csharp_plugin greet.proto

# Generate with async suffix
protoc --csharp_out=./Server/ --grpc_out=./Server/ --plugin=protoc-gen-grpc=grpc_csharp_plugin --grpc_opt=append_async_suffix=true greet.proto

echo "Generated C# code with and without async suffixes"