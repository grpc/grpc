#!/bin/bash

set -x

VERSION=$(git rev-parse HEAD)

PROJECT=grpc-testing
TAG=gcr.io/${PROJECT}/python_xds_interop_client:$VERSION

cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.."

docker build \
    -t ${TAG} \
    -f src/python/grpcio_tests/tests_py3_only/interop/Dockerfile.client \
    .
