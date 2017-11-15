#!/bin/bash
# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -ex

cd $(dirname $0)/../../..

export GRPC_PYTHON_USE_CUSTOM_BDIST=0
export GRPC_PYTHON_BUILD_WITH_CYTHON=1
export PYTHON=${PYTHON:-python}
export PIP=${PIP:-pip}
export AUDITWHEEL=${AUDITWHEEL:-auditwheel}

mkdir -p "${ARTIFACTS_OUT}"
ARTIFACT_DIR="$PWD/${ARTIFACTS_OUT}"

# Build the source distribution first because MANIFEST.in cannot override
# exclusion of built shared objects among package resources (for some
# inexplicable reason).
${SETARCH_CMD} ${PYTHON} setup.py sdist

# Wheel has a bug where directories don't get excluded.
# https://bitbucket.org/pypa/wheel/issues/99/cannot-exclude-directory
${SETARCH_CMD} ${PYTHON} setup.py bdist_wheel

# Build gRPC tools package distribution
${PYTHON} tools/distrib/python/make_grpcio_tools.py

# Build gRPC tools package source distribution
${SETARCH_CMD} ${PYTHON} tools/distrib/python/grpcio_tools/setup.py sdist

# Build gRPC tools package binary distribution
${SETARCH_CMD} ${PYTHON} tools/distrib/python/grpcio_tools/setup.py bdist_wheel

if [ "$GRPC_BUILD_MANYLINUX_WHEEL" != "" ]
then
  for wheel in dist/*.whl; do
    ${AUDITWHEEL} repair $wheel -w "$ARTIFACT_DIR"
    rm $wheel
  done
  for wheel in tools/distrib/python/grpcio_tools/dist/*.whl; do
    ${AUDITWHEEL} repair $wheel -w "$ARTIFACT_DIR"
    rm $wheel
  done
fi

# We need to use the built grpcio-tools/grpcio to compile the protos
# Wheels are not supported by setup_requires/dependency_links, so we
# manually install the dependency.  Note we should only do this if we
# are in a docker image or in a virtualenv.
#
# Please note that since the packages built inside this code path
# are pure Python, only one package needs to be distributed on PyPI
# for all platforms, so it is fine that they are only built on one
# platform, i.e. Linux.
if [ "$GRPC_BUILD_GRPCIO_TOOLS_DEPENDENTS" != "" ]
then
  ${PIP} install -rrequirements.txt
  ${PIP} install grpcio --no-index --find-links "file://$ARTIFACT_DIR/"
  ${PIP} install grpcio-tools --no-index --find-links "file://$ARTIFACT_DIR/"

  # preprocess health.proto and generate the required pb2 and pb2_grpc
  # files for grpcio_health_checking package
  HEALTH_PROTO="src/proto/grpc/health/v1/health.proto"
  HEALTH_PB2_DIR="src/python/grpcio_health_checking/grpc_health/v1"
  ${PYTHON} -m grpc_tools.protoc "${HEALTH_PROTO}" \
    "-I$(dirname "${HEALTH_PROTO}")" \
    "--python_out=${HEALTH_PB2_DIR}" \
    "--grpc_python_out=${HEALTH_PB2_DIR}"
  # Build gRPC health-checking source distribution
  ${SETARCH_CMD} ${PYTHON} src/python/grpcio_health_checking/setup.py sdist
  cp -r src/python/grpcio_health_checking/dist/* "$ARTIFACT_DIR"

  # preprocess reflection.proto and generate the required pb2 and pb2_grpc
  # files for grpcio_reflection package
  REFLECTION_PROTO="src/proto/grpc/reflection/v1alpha/reflection.proto"
  REFLECTION_PB2_DIR="src/python/grpcio_reflection/grpc_reflection/v1alpha"
  ${PYTHON} -m grpc_tools.protoc "${REFLECTION_PROTO}" \
    "-I$(dirname "${REFLECTION_PROTO}")" \
    "--python_out=${REFLECTION_PB2_DIR}" \
    "--grpc_python_out=${REFLECTION_PB2_DIR}"
  # Build gRPC reflection source distribution
  ${SETARCH_CMD} ${PYTHON} src/python/grpcio_reflection/setup.py sdist
  cp -r src/python/grpcio_reflection/dist/* "$ARTIFACT_DIR"
fi

cp -r dist/* "$ARTIFACT_DIR"
cp -r tools/distrib/python/grpcio_tools/dist/* "$ARTIFACT_DIR"
