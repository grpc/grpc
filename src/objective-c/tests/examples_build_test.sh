#!/bin/bash

set -ex

SCHEME=HelloWorld EXAMPLE_PATH=examples/objective-c/helloworld ./build_one_example.sh
SCHEME=RouteGuideClient EXAMPLE_PATH=examples/objective-c/route_guide ./build_one_example.sh
SCHEME=AuthSample EXAMPLE_PATH=examples/objective-c/auth_sample ./build_one_example.sh
