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

if [ "$GRPC_SKIP_PIP_CYTHON_UPGRADE" == "" ]
then
  # Install Cython to avoid source wheel build failure.
  # This only needs to be done when not running under docker (=on MacOS)
  # since the docker images used for building python wheels
  # already have a new-enough version of cython pre-installed.
  # Any installation step is a potential source of breakages,
  # so we are trying to perform as few download-and-install operations
  # as possible.
  "${PIP}" install --upgrade cython
fi

# Allow build_ext to build C/C++ files in parallel
# by enabling a monkeypatch. It speeds up the build a lot.
# Use externally provided GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS value if set.
export GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS=${GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS:-2}

mkdir -p "${ARTIFACTS_OUT}"
ARTIFACT_DIR="$PWD/${ARTIFACTS_OUT}"

# check whether we are crosscompiling. AUDITWHEEL_ARCH is set by the dockcross docker image.
if [ "$AUDITWHEEL_ARCH" == "aarch64" ]
then
  # when crosscompiling for aarch64, --plat-name needs to be set explicitly
  # to end up with correctly named wheel file
  # the value should be manylinuxABC_ARCH and dockcross docker image
  # conveniently provides the value in the AUDITWHEEL_PLAT env
  WHEEL_PLAT_NAME_FLAG="--plat-name=$AUDITWHEEL_PLAT"

  # override the value of EXT_SUFFIX to make sure the crosscompiled .so files in the wheel have the correct filename suffix
  GRPC_PYTHON_OVERRIDE_EXT_SUFFIX="$(${PYTHON} -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX").replace("-x86_64-linux-gnu.so", "-aarch64-linux-gnu.so"))')"
  export GRPC_PYTHON_OVERRIDE_EXT_SUFFIX

  # since we're crosscompiling, we need to explicitly choose the right platform for boringssl assembly optimizations
  export GRPC_BUILD_OVERRIDE_BORING_SSL_ASM_PLATFORM="linux-aarch64"
fi

# check whether we are crosscompiling. AUDITWHEEL_ARCH is set by the dockcross docker image.
if [ "$AUDITWHEEL_ARCH" == "armv7l" ]
then
  # when crosscompiling for arm, --plat-name needs to be set explicitly
  # to end up with correctly named wheel file
  # our dockcross-based docker image onveniently provides the value in the AUDITWHEEL_PLAT env
  WHEEL_PLAT_NAME_FLAG="--plat-name=$AUDITWHEEL_PLAT"

  # override the value of EXT_SUFFIX to make sure the crosscompiled .so files in the wheel have the correct filename suffix
  GRPC_PYTHON_OVERRIDE_EXT_SUFFIX="$(${PYTHON} -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX").replace("-x86_64-linux-gnu.so", "-arm-linux-gnueabihf.so"))')"
  export GRPC_PYTHON_OVERRIDE_EXT_SUFFIX

  # since we're crosscompiling, we need to explicitly choose the right platform for boringssl assembly optimizations
  export GRPC_BUILD_OVERRIDE_BORING_SSL_ASM_PLATFORM="linux-arm"
fi

# Build the source distribution first because MANIFEST.in cannot override
# exclusion of built shared objects among package resources (for some
# inexplicable reason).
${SETARCH_CMD} "${PYTHON}" setup.py sdist

# Wheel has a bug where directories don't get excluded.
# https://bitbucket.org/pypa/wheel/issues/99/cannot-exclude-directory
# shellcheck disable=SC2086
${SETARCH_CMD} "${PYTHON}" setup.py bdist_wheel $WHEEL_PLAT_NAME_FLAG

GRPCIO_STRIP_TEMPDIR=$(mktemp -d)
GRPCIO_TAR_GZ_LIST=( dist/grpcio-*.tar.gz )
GRPCIO_TAR_GZ=${GRPCIO_TAR_GZ_LIST[0]}
GRPCIO_STRIPPED_TAR_GZ=$(mktemp -t "XXXXXXXXXX.tar.gz")

clean_non_source_files() {
( cd "$1"
  find . -type f \
    | grep -v '\.c$' | grep -v '\.cc$' | grep -v '\.cpp$' \
    | grep -v '\.h$' | grep -v '\.hh$' | grep -v '\.inc$' \
    | grep -v '\.s$' | grep -v '\.py$' | grep -v '\.hpp$' \
    | grep -v '\.S$' | grep -v '\.asm$'                   \
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
# shellcheck disable=SC2086
${SETARCH_CMD} "${PYTHON}" tools/distrib/python/grpcio_tools/setup.py bdist_wheel $WHEEL_PLAT_NAME_FLAG

if [ "$GRPC_RUN_AUDITWHEEL_REPAIR" != "" ]
then
  for wheel in dist/*.whl; do
    "${AUDITWHEEL}" show "$wheel" | tee /dev/stderr |  grep -E -w "$AUDITWHEEL_PLAT"
    "${AUDITWHEEL}" repair "$wheel" -w "$ARTIFACT_DIR"
    rm "$wheel"
  done
  for wheel in tools/distrib/python/grpcio_tools/dist/*.whl; do
    "${AUDITWHEEL}" show "$wheel" | tee /dev/stderr |  grep -E -w "$AUDITWHEEL_PLAT"
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
    "${PIP}" install futures>=2.2.0 enum34>=1.0.4
  fi

  "${PIP}" install grpcio --no-index --find-links "file://$ARTIFACT_DIR/"
  "${PIP}" install grpcio-tools --no-index --find-links "file://$ARTIFACT_DIR/"

  # Note(lidiz) setuptools's "sdist" command creates a source tarball, which
  # demands an extra step of building the wheel. The building step is merely ran
  # through setup.py, but we can optimize it with "bdist_wheel" command, which
  # skips the wheel building step.

  # Build grpcio_testing source distribution
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_testing/setup.py preprocess \
      sdist bdist_wheel
  cp -r src/python/grpcio_testing/dist/* "$ARTIFACT_DIR"

  # Build grpcio_channelz source distribution
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_channelz/setup.py \
      preprocess build_package_protos sdist bdist_wheel
  cp -r src/python/grpcio_channelz/dist/* "$ARTIFACT_DIR"

  # Build grpcio_health_checking source distribution
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_health_checking/setup.py \
      preprocess build_package_protos sdist bdist_wheel
  cp -r src/python/grpcio_health_checking/dist/* "$ARTIFACT_DIR"

  # Build grpcio_reflection source distribution
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_reflection/setup.py \
      preprocess build_package_protos sdist bdist_wheel
  cp -r src/python/grpcio_reflection/dist/* "$ARTIFACT_DIR"

  # Build grpcio_status source distribution
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_status/setup.py \
      preprocess sdist bdist_wheel
  cp -r src/python/grpcio_status/dist/* "$ARTIFACT_DIR"

  # Build grpcio_csds source distribution
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_csds/setup.py \
      sdist bdist_wheel
  cp -r src/python/grpcio_csds/dist/* "$ARTIFACT_DIR"

  # Build grpcio_admin source distribution and it needs the cutting-edge version
  # of Channelz and CSDS to be installed.
  "${PIP}" install --upgrade xds-protos==0.0.8
  "${PIP}" install grpcio-channelz --no-index --find-links "file://$ARTIFACT_DIR/"
  "${PIP}" install grpcio-csds --no-index --find-links "file://$ARTIFACT_DIR/"
  ${SETARCH_CMD} "${PYTHON}" src/python/grpcio_admin/setup.py \
      sdist bdist_wheel
  cp -r src/python/grpcio_admin/dist/* "$ARTIFACT_DIR"
fi

if [ "$GRPC_SKIP_TWINE_CHECK" == "" ]
then
  # Ensure the generated artifacts are valid.
  "${PYTHON}" -m pip install virtualenv
  "${PYTHON}" -m virtualenv venv || { "${PYTHON}" -m pip install virtualenv==16.7.9 && "${PYTHON}" -m virtualenv venv; }
  venv/bin/python -m pip install "twine<=2.0"
  venv/bin/python -m twine check dist/* tools/distrib/python/grpcio_tools/dist/*
  rm -rf venv/
fi

cp -r dist/* "$ARTIFACT_DIR"
cp -r tools/distrib/python/grpcio_tools/dist/* "$ARTIFACT_DIR"
