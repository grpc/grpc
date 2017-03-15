#!/bin/bash
grpc_tools_ruby_protoc -I protos --ruby_out=lib --grpc_out=lib protos/echo.proto protos/client_control.proto
