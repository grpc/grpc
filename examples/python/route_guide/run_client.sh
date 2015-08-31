#!/bin/bash

# This is where you have cloned out the https://github.com/grpc/grpc repository
# And built gRPC Python.
# ADJUST THIS PATH TO WHERE YOUR ACTUAL LOCATION IS
GRPC_ROOT=~/github/grpc

$GRPC_ROOT/python2.7_virtual_environment/bin/python -B route_guide_client.py
