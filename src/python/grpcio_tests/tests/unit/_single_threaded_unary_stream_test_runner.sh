#!/bin/bash

export GRPC_SINGLE_THREADED_UNARY_STREAM=true

find . -name "$1" -exec {} \;
