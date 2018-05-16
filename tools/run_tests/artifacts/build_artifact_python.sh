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

cd "$(dirname "$0")/../../.."

export GRPC_PYTHON_BUILD_WITH_CYTHON=1
export PYTHON=${PYTHON:-python}
export PIP=${PIP:-pip}
export AUDITWHEEL=${AUDITWHEEL:-auditwheel}

mkdir -p "${ARTIFACTS_OUT}"
ARTIFACT_DIR="$PWD/${ARTIFACTS_OUT}"

# Build the source distribution first because MANIFEST.in cannot override
# exclusion of built shared objects among package resources (for some
# inexplicable reason).
${SETARCH_CMD} "${PYTHON}" setup.py sdist

# Wheel has a bug where directories don't get excluded.
# https://bitbucket.org/pypa/wheel/issues/99/cannot-exclude-directory
${SETARCH_CMD} "${PYTHON}" setup.py bdist_wheel

GRPCIO_STRIP_TEMPDIR=$(mktemp -d)
GRPCIO_TAR_GZ_LIST=( dist/grpcio-*.tar.gz )
GRPCIO_TAR_GZ=${GRPCIO_TAR_GZ_LIST[0]}
GRPCIO_STRIPPED_TAR_GZ=$(mktemp -t "XXXXXXXXXX.tar.gz")

clean_non_source_files() {
( cd "$1"
  find . -type f \
    | grep -v '\.c$' | grep -v '\.cc$' | grep -v '\.cpp$' \
    | grep -v '\.h$' | grep -v '\.hh$' \
    | grep -v '\.s$' | grep -v '\.py$' \
    | while read -r file; do
      rm -f "$file" || true
    done
  find . -type d -empty -delete
)
}

tar xzf "${GRPCIO_TAR_GZ}" -C "${GRPCIO_STRIP_TEMPDIR}"
( cd "${GRPCIO_STRIP_TEMPDIR}"
  find . -type d -name .git -exec rm -fr {} \; || true
  for dir in */third_party/*; do
    clean_non_source_files "${dir}" || true
  done
  tar czf "${GRPCIO_STRIPPED_TAR_GZ}" -- *
)
mv "${GRPCIO_STRIPPED_TAR_GZ}" "${GRPCIO_TAR_GZ}"

# Build gRPC tools package distribution
"${PYTHON}" tools/distrib/python/make_grpcio_tools.py

# Build gRPC tools package source distribution
${SETARCH_CMD} "${PYTHON}" tools/distrib/python/grpcio_tools/setup.py sdist

# Build gRPC tools package binary distribution
${SETARCH_CMD} "${PYTHON}" tools/distrib/python/grpcio_tools/setup.py bdist_wheel

if [ "$GRPC_BUILD_MANYLINUX_WHEEL" != "" ]
then
  for wheel in dist/*.whl; do
    "${AUDITWHEEL}" repair "$wheel" -w "$ARTIFACT_DIR"
    rm "$wheel"
  done
  for wheel in tools/distrib/python/grpcio_tools/dist/*.whl; do
    "${AUDITWHEEL}" repair "$wheel" -w "$ARTIFACT_DIR"
    rm "$wheel"
  done
fi

# We need to use the built grpcio-tools/grpcio to compile the health proto
# Wheels are not supported by setup_requires/dependency_links, so we
# manually install the dependency.  Note we should only do this if we
# are in a docker image or in a virtualenv.
if [ "$GRPC_BUILD_GRPCIO_TOOLS_DEPENDENTS" != "" ]
then
  "${PIP}" install -rrequirements.txt

  if [ "$("$PYTHON" -c "import sys; print(sys.version_info[0])")" == "2" ]
  then
    "${PIP}" install futures>=2.2.0
  fi

  "${PIP}" install grpcio --no-index --find-links "file://$ARTIFACT_DIR/"
  "${PIP}" install grpcio-tools --no-index --find-links "file://$ARTIFACT_DIR/"

  # Build gRPC health-checking source distribution
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_health_checking/setup.py \
      preprocess build_package_protos sdist
  cp -r src/python/grpcio_health_checking/dist/* "$ARTIFACT_DIR"

  # Build gRPC reflection source distribution
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_reflection/setup.py \
      preprocess build_package_protos sdist
  cp -r src/python/grpcio_reflection/dist/* "$ARTIFACT_DIR"
fi

cp -r dist/* "$ARTIFACT_DIR"
cp -r tools/distrib/python/grpcio_tools/dist/* "$ARTIFACT_DIR"
