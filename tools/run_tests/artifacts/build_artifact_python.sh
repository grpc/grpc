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
export AUDITWHEEL=${AUDITWHEEL:-auditwheel}

# activate ccache if desired
# shellcheck disable=SC1091
source tools/internal_ci/helper_scripts/prepare_ccache_symlinks_rc

# Install uv for faster package management (if available)
if command -v uv >/dev/null 2>&1; then
  echo "Using uv for faster package installation"
  UV_CMD="uv"
else
  echo "uv not available, attempting to install it for faster builds"
  if curl -LsSf https://astral.sh/uv/install.sh | sh; then
    # Add uv to PATH - it installs to $HOME/.local/bin
    export PATH="$HOME/.local/bin:$PATH"
    # Also try to source cargo env as fallback
    source $HOME/.cargo/env 2>/dev/null || true
    if command -v uv >/dev/null 2>&1; then
      echo "Successfully installed uv"
      UV_CMD="uv"
    else
      echo "Failed to install uv, falling back to pip"
      UV_CMD="pip"
      "${PYTHON}" -m pip install --upgrade pip
    fi
  else
    echo "Failed to install uv, falling back to pip"
    UV_CMD="pip"
    "${PYTHON}" -m pip install --upgrade pip
  fi
fi

# Install build dependencies using uv or pip
if [ "$UV_CMD" = "uv" ]; then
  # Use --no-deps to avoid dependency conflicts with existing packages
  uv pip install --system --no-deps setuptools==69.5.1 wheel==0.43.0 build
else
  "${PYTHON}" -m pip install setuptools==69.5.1 wheel==0.43.0 build
fi

if [ "$GRPC_SKIP_PIP_CYTHON_UPGRADE" == "" ]
then
  # Install Cython to avoid source wheel build failure.
  # This only needs to be done when not running under docker (=on MacOS)
  # since the docker images used for building python wheels
  # already have a new-enough version of cython pre-installed.
  # Any installation step is a potential source of breakages,
  # so we are trying to perform as few download-and-install operations
  # as possible.
  if [ "$UV_CMD" = "uv" ]; then
    uv pip install --system --no-deps --upgrade 'cython==3.1.1'
  else
    "${PYTHON}" -m pip install --upgrade 'cython==3.1.1'
  fi
  
  # Install Rust compiler for cryptography package
  echo "Installing Rust compiler for cryptography package"
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
  source ~/.cargo/env
  
  # Also upgrade pip to ensure we can use prebuilt wheels when available
  if [ "$UV_CMD" = "pip" ]; then
    "${PYTHON}" -m pip install --upgrade pip
  fi
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
  # when crosscompiling for aarch64, set _PYTHON_HOST_PLATFORM for modern build system
  # the value should be manylinuxABC_ARCH and dockcross docker image
  # conveniently provides the value in the AUDITWHEEL_PLAT env
  export _PYTHON_HOST_PLATFORM="$AUDITWHEEL_PLAT"

  # override the value of EXT_SUFFIX to make sure the crosscompiled .so files in the wheel have the correct filename suffix
  GRPC_PYTHON_OVERRIDE_EXT_SUFFIX="$(${PYTHON} -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX").replace("-x86_64-linux-gnu.so", "-aarch64-linux-gnu.so"))')"
  export GRPC_PYTHON_OVERRIDE_EXT_SUFFIX

  # since we're crosscompiling, we need to explicitly choose the right platform for boringssl assembly optimizations
  export GRPC_BUILD_OVERRIDE_BORING_SSL_ASM_PLATFORM="linux-aarch64"
fi

# check whether we are crosscompiling. AUDITWHEEL_ARCH is set by the dockcross docker image.
if [ "$AUDITWHEEL_ARCH" == "armv7l" ]
then
  # when crosscompiling for arm, set _PYTHON_HOST_PLATFORM for modern build system
  # our dockcross-based docker image conveniently provides the value in the AUDITWHEEL_PLAT env
  export _PYTHON_HOST_PLATFORM="$AUDITWHEEL_PLAT"

  # override the value of EXT_SUFFIX to make sure the crosscompiled .so files in the wheel have the correct filename suffix
  GRPC_PYTHON_OVERRIDE_EXT_SUFFIX="$(${PYTHON} -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX").replace("-x86_64-linux-gnu.so", "-arm-linux-gnueabihf.so"))')"
  export GRPC_PYTHON_OVERRIDE_EXT_SUFFIX

  # since we're crosscompiling, we need to explicitly choose the right platform for boringssl assembly optimizations
  export GRPC_BUILD_OVERRIDE_BORING_SSL_ASM_PLATFORM="linux-arm"
fi

ancillary_package_dir=(
  "src/python/grpcio_admin/"
  "src/python/grpcio_channelz/"
  "src/python/grpcio_csds/"
  "src/python/grpcio_health_checking/"
  "src/python/grpcio_reflection/"
  "src/python/grpcio_status/"
  "src/python/grpcio_testing/"
  "src/python/grpcio_observability/"
  "src/python/grpcio_csm_observability/"
)

# Copy license to ancillary package directories so it will be distributed.
for directory in "${ancillary_package_dir[@]}"; do
  cp "LICENSE" "${directory}"
done

# Build the source distribution and wheel using modern build system
# This replaces the deprecated setup.py sdist and bdist_wheel commands
echo "DEBUG: Starting Python build process"
echo "DEBUG: PYTHON=$PYTHON"
echo "DEBUG: SETARCH_CMD=$SETARCH_CMD"
echo "DEBUG: Current directory: $(pwd)"
echo "DEBUG: Contents of current directory:"
ls -la
echo "DEBUG: Using --no-build-isolation flag to prevent Cython import issues"
if [ "$UV_CMD" = "uv" ]; then
  ${SETARCH_CMD} uv build --no-build-isolation
else
  ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
fi
echo "DEBUG: Build completed, checking dist/ directory:"
ls -la dist/ 2>/dev/null || echo "DEBUG: dist/ directory not found"

GRPCIO_STRIP_TEMPDIR=$(mktemp -d)
GRPCIO_TAR_GZ_LIST=( dist/grpcio-*.tar.gz )
GRPCIO_TAR_GZ=${GRPCIO_TAR_GZ_LIST[0]}
GRPCIO_STRIPPED_TAR_GZ=$(mktemp -t "TAR_GZ_XXXXXXXXXX")

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
  chmod ugo+r "${GRPCIO_STRIPPED_TAR_GZ}"
)
mv "${GRPCIO_STRIPPED_TAR_GZ}" "${GRPCIO_TAR_GZ}"

# Build gRPC tools package distribution
echo "DEBUG: Building gRPC tools package"
"${PYTHON}" tools/distrib/python/make_grpcio_tools.py

# Build gRPC tools package using modern build system
echo "DEBUG: Building gRPC tools wheel"
echo "DEBUG: Using --no-build-isolation flag to prevent Cython import issues"
cd tools/distrib/python/grpcio_tools
if [ "$UV_CMD" = "uv" ]; then
  ${SETARCH_CMD} uv build --no-build-isolation
else
  ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
fi
cd -
echo "DEBUG: gRPC tools build completed, checking tools/distrib/python/grpcio_tools/dist/:"
ls -la tools/distrib/python/grpcio_tools/dist/ 2>/dev/null || echo "DEBUG: tools/distrib/python/grpcio_tools/dist/ not found"

if [ "$GRPC_BUILD_MAC" == "" ]; then
  "${PYTHON}" src/python/grpcio_observability/make_grpcio_observability.py
  cd src/python/grpcio_observability
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
fi


# run twine check before auditwheel, because auditwheel puts the repaired wheels into
# the artifacts output dir.
if [ "$GRPC_SKIP_TWINE_CHECK" == "" ]
then
  # Install virtualenv if it isn't already available.
  # TODO(jtattermusch): cleanup the virtualenv version fallback logic.
  if [ "$UV_CMD" = "uv" ]; then
    uv pip install --system --no-deps virtualenv
    "${PYTHON}" -m virtualenv venv || { uv pip install --system --no-deps virtualenv==20.0.23 && "${PYTHON}" -m virtualenv venv; }
    # Ensure the generated artifacts are valid using "twine check"
    venv/bin/python -m pip install "cryptography==40.0.0" "twine==5.0.0" "readme_renderer<40.0"
  else
    "${PYTHON}" -m pip install virtualenv
    "${PYTHON}" -m virtualenv venv || { "${PYTHON}" -m pip install virtualenv==20.0.23 && "${PYTHON}" -m virtualenv venv; }
    # Ensure the generated artifacts are valid using "twine check"
    venv/bin/python -m pip install "cryptography==40.0.0" "twine==5.0.0" "readme_renderer<40.0"
  fi
  venv/bin/python -m twine check dist/* tools/distrib/python/grpcio_tools/dist/*
  if [ "$GRPC_BUILD_MAC" == "" ]; then
    venv/bin/python -m twine check src/python/grpcio_observability/dist/*
  fi
  rm -rf venv/
fi

assert_is_universal_wheel()  {
  WHL="$1"
  TMPDIR=$(mktemp -d)
  unzip "$WHL" -d "$TMPDIR"
  SO=$(find "$TMPDIR" -name '*.so' | head -n1)
  if ! file "$SO" | grep "Mach-O universal binary with 2 architectures"; then
    echo "$WHL is not universal2. Found the following:" >/dev/stderr
    file "$SO" >/dev/stderr
    exit 1
  fi
}

fix_faulty_universal2_wheel() {
  WHL="$1"
  assert_is_universal_wheel "$WHL"
  if echo "$WHL" | grep "x86_64"; then
    UPDATED_NAME="${WHL//x86_64/universal2}"
    mv "$WHL" "$UPDATED_NAME"
  fi
}

# This is necessary due to https://github.com/pypa/wheel/issues/406.
# wheel incorrectly generates a universal2 artifact that only contains
# x86_64 libraries.
if [ "$GRPC_BUILD_MAC" != "" ]; then
  for WHEEL in dist/*.whl tools/distrib/python/grpcio_tools/dist/*.whl; do
    fix_faulty_universal2_wheel "$WHEEL"
  done
fi


if [ "$GRPC_RUN_AUDITWHEEL_REPAIR" != "" ]
then
  echo "DEBUG: Running auditwheel repair, ARTIFACT_DIR=$ARTIFACT_DIR"
  echo "DEBUG: Wheel files in dist/:"
  ls -la dist/*.whl 2>/dev/null || echo "DEBUG: No wheel files found in dist/"
  for wheel in dist/*.whl; do
    echo "DEBUG: Processing wheel: $wheel"
    "${AUDITWHEEL}" show "$wheel" | tee /dev/stderr |  grep -E -w "$AUDITWHEEL_PLAT"
    "${AUDITWHEEL}" repair "$wheel" --strip --wheel-dir "$ARTIFACT_DIR"
    rm "$wheel"
  done
  for wheel in tools/distrib/python/grpcio_tools/dist/*.whl; do
    "${AUDITWHEEL}" show "$wheel" | tee /dev/stderr |  grep -E -w "$AUDITWHEEL_PLAT"
    "${AUDITWHEEL}" repair "$wheel" --strip --wheel-dir "$ARTIFACT_DIR"
    rm "$wheel"
  done
  # Copy repaired wheels to parent directory for distribtest compatibility
  cp -r "$ARTIFACT_DIR"/*.whl "$(dirname "$ARTIFACT_DIR")/" 2>/dev/null || true
  # Also copy to input_artifacts if it exists (for distribtest compatibility)
  if [ -d "${EXTERNAL_GIT_ROOT}/input_artifacts" ]; then
    cp -r "$ARTIFACT_DIR"/*.whl "${EXTERNAL_GIT_ROOT}/input_artifacts/" 2>/dev/null || true
  fi
else
  echo "DEBUG: Not running auditwheel repair, ARTIFACT_DIR=$ARTIFACT_DIR"
  echo "DEBUG: Wheel files in dist/:"
  ls -la dist/*.whl 2>/dev/null || echo "DEBUG: No wheel files found in dist/"
  echo "DEBUG: Copying wheel files to ARTIFACT_DIR"
  cp -r dist/*.whl "$ARTIFACT_DIR"
  cp -r tools/distrib/python/grpcio_tools/dist/*.whl "$ARTIFACT_DIR"
  echo "DEBUG: Contents of ARTIFACT_DIR after copying:"
  ls -la "$ARTIFACT_DIR" 2>/dev/null || echo "DEBUG: Failed to list ARTIFACT_DIR"
  # Also copy wheel files to parent directory for distribtest compatibility
  cp -r dist/*.whl "$(dirname "$ARTIFACT_DIR")/"
  cp -r tools/distrib/python/grpcio_tools/dist/*.whl "$(dirname "$ARTIFACT_DIR")/"
  # Also copy to input_artifacts if it exists (for distribtest compatibility)
  if [ -d "${EXTERNAL_GIT_ROOT}/input_artifacts" ]; then
    cp -r dist/*.whl "${EXTERNAL_GIT_ROOT}/input_artifacts/" 2>/dev/null || true
    cp -r tools/distrib/python/grpcio_tools/dist/*.whl "${EXTERNAL_GIT_ROOT}/input_artifacts/" 2>/dev/null || true
  fi
fi

# grpcio and grpcio-tools have already been copied to artifact_dir
# by "auditwheel repair", now copy the .tar.gz source archives as well.
cp -r dist/*.tar.gz "$ARTIFACT_DIR"
cp -r tools/distrib/python/grpcio_tools/dist/*.tar.gz "$ARTIFACT_DIR"

# Ensure wheel files are copied to ARTIFACT_DIR (fallback for cases where auditwheel repair didn't run)
if [ ! -f "$ARTIFACT_DIR"/*.whl ] 2>/dev/null; then
  echo "DEBUG: No wheel files found in ARTIFACT_DIR, copying from dist/"
  cp -r dist/*.whl "$ARTIFACT_DIR" 2>/dev/null || echo "DEBUG: No wheel files in dist/"
  cp -r tools/distrib/python/grpcio_tools/dist/*.whl "$ARTIFACT_DIR" 2>/dev/null || echo "DEBUG: No grpcio-tools wheel files"
fi


if [ "$GRPC_BUILD_MAC" == "" ]; then
  if [ "$GRPC_RUN_AUDITWHEEL_REPAIR" != "" ]
  then
    for wheel in src/python/grpcio_observability/dist/*.whl; do
      "${AUDITWHEEL}" show "$wheel" | tee /dev/stderr |  grep -E -w "$AUDITWHEEL_PLAT"
      "${AUDITWHEEL}" repair "$wheel" --strip --wheel-dir "$ARTIFACT_DIR"
      rm "$wheel"
    done
    # Copy repaired observability wheels to parent directory for distribtest compatibility
    cp -r "$ARTIFACT_DIR"/*observability*.whl "$(dirname "$ARTIFACT_DIR")/" 2>/dev/null || true
  else
    cp -r src/python/grpcio_observability/dist/*.whl "$ARTIFACT_DIR"
    # Also copy observability wheel files to parent directory for distribtest compatibility
    cp -r src/python/grpcio_observability/dist/*.whl "$(dirname "$ARTIFACT_DIR")/"
  fi
  cp -r src/python/grpcio_observability/dist/*.tar.gz "$ARTIFACT_DIR"

  # Build grpcio_csm_observability distribution
  if [ "$GRPC_BUILD_MAC" == "" ]; then
    cd src/python/grpcio_csm_observability
    if [ "$UV_CMD" = "uv" ]; then
      ${SETARCH_CMD} uv build --no-build-isolation
    else
      ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
    fi
    cd -
    cp -r src/python/grpcio_csm_observability/dist/* "$ARTIFACT_DIR"
  fi
fi

# We need to use the built grpcio-tools/grpcio to compile the health proto
# Wheels are not supported by setup_requires/dependency_links, so we
# manually install the dependency.  Note we should only do this if we
# are in a docker image or in a virtualenv.
if [ "$GRPC_BUILD_GRPCIO_TOOLS_DEPENDENTS" != "" ]
then
  if [ "$UV_CMD" = "uv" ]; then
    uv pip install --system --no-deps -rrequirements.txt
  else
    "${PYTHON}" -m pip install -rrequirements.txt
  fi

  if [ "$("$PYTHON" -c "import sys; print(sys.version_info[0])")" == "2" ]
  then
    # shellcheck disable=SC2261
    if [ "$UV_CMD" = "uv" ]; then
      uv pip install --system --no-deps futures>=2.2.0 enum34>=1.0.4
    else
      "${PYTHON}" -m pip install futures>=2.2.0 enum34>=1.0.4
    fi
  fi

  if [ "$UV_CMD" = "uv" ]; then
    uv pip install --system --no-deps grpcio --no-index --find-links "file://$ARTIFACT_DIR/"
    uv pip install --system --no-deps grpcio-tools --no-index --find-links "file://$ARTIFACT_DIR/"
  else
    "${PYTHON}" -m pip install grpcio --no-index --find-links "file://$ARTIFACT_DIR/"
    "${PYTHON}" -m pip install grpcio-tools --no-index --find-links "file://$ARTIFACT_DIR/"
  fi

  # Note(lidiz) setuptools's "sdist" command creates a source tarball, which
  # demands an extra step of building the wheel. The building step is merely ran
  # through setup.py, but we can optimize it with "bdist_wheel" command, which
  # skips the wheel building step.

  # Build xds_protos source distribution
  # build.py is invoked as part of generate_projects.
  cd tools/distrib/python/xds_protos
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
  cp -r tools/distrib/python/xds_protos/dist/* "$ARTIFACT_DIR"

  # Build grpcio_testing source distribution
  cd src/python/grpcio_testing
  ${SETARCH_CMD} "${PYTHON}" setup.py preprocess
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
  cp -r src/python/grpcio_testing/dist/* "$ARTIFACT_DIR"

  # Build grpcio_channelz source distribution
  cd src/python/grpcio_channelz
  ${SETARCH_CMD} "${PYTHON}" setup.py preprocess build_package_protos
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
  cp -r src/python/grpcio_channelz/dist/* "$ARTIFACT_DIR"

  # Build grpcio_health_checking source distribution
  cd src/python/grpcio_health_checking
  ${SETARCH_CMD} "${PYTHON}" setup.py preprocess build_package_protos
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
  cp -r src/python/grpcio_health_checking/dist/* "$ARTIFACT_DIR"

  # Build grpcio_reflection source distribution
  cd src/python/grpcio_reflection
  ${SETARCH_CMD} "${PYTHON}" setup.py preprocess build_package_protos
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
  cp -r src/python/grpcio_reflection/dist/* "$ARTIFACT_DIR"

  # Build grpcio_status source distribution
  cd src/python/grpcio_status
  ${SETARCH_CMD} "${PYTHON}" setup.py preprocess
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
  cp -r src/python/grpcio_status/dist/* "$ARTIFACT_DIR"

  # Install xds-protos as a dependency of grpcio-csds
  if [ "$UV_CMD" = "uv" ]; then
    uv pip install --system --no-deps xds-protos --no-index --find-links "file://$ARTIFACT_DIR/"
  else
    "${PYTHON}" -m pip install xds-protos --no-index --find-links "file://$ARTIFACT_DIR/"
  fi

  # Build grpcio_csds source distribution
  cd src/python/grpcio_csds
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
  cp -r src/python/grpcio_csds/dist/* "$ARTIFACT_DIR"

  # Build grpcio_admin source distribution and it needs the cutting-edge version
  # of Channelz and CSDS to be installed.
  if [ "$UV_CMD" = "uv" ]; then
    uv pip install --system --no-deps grpcio-channelz --no-index --find-links "file://$ARTIFACT_DIR/"
    uv pip install --system --no-deps grpcio-csds --no-index --find-links "file://$ARTIFACT_DIR/"
  else
    "${PYTHON}" -m pip install grpcio-channelz --no-index --find-links "file://$ARTIFACT_DIR/"
    "${PYTHON}" -m pip install grpcio-csds --no-index --find-links "file://$ARTIFACT_DIR/"
  fi
  cd src/python/grpcio_admin
  if [ "$UV_CMD" = "uv" ]; then
    ${SETARCH_CMD} uv build --no-build-isolation
  else
    ${SETARCH_CMD} "${PYTHON}" -m build --no-isolation
  fi
  cd -
  cp -r src/python/grpcio_admin/dist/* "$ARTIFACT_DIR"

fi

# Final fallback: Ensure wheel files are copied to ARTIFACT_DIR
# This handles cases where the wheel copying logic above didn't work
echo "DEBUG: Final fallback - ensuring wheel files are in ARTIFACT_DIR"
if [ -d "$ARTIFACT_DIR" ]; then
  echo "DEBUG: ARTIFACT_DIR exists: $ARTIFACT_DIR"
  echo "DEBUG: Current contents of ARTIFACT_DIR:"
  ls -la "$ARTIFACT_DIR" 2>/dev/null || echo "DEBUG: Failed to list ARTIFACT_DIR"
  
  # Check if wheel files exist in dist/ and copy them if ARTIFACT_DIR is empty of wheels
  if [ -d "dist" ] && [ ! -f "$ARTIFACT_DIR"/*.whl ] 2>/dev/null; then
    echo "DEBUG: No wheel files in ARTIFACT_DIR, copying from dist/"
    cp -r dist/*.whl "$ARTIFACT_DIR" 2>/dev/null || echo "DEBUG: No wheel files in dist/"
    cp -r tools/distrib/python/grpcio_tools/dist/*.whl "$ARTIFACT_DIR" 2>/dev/null || echo "DEBUG: No grpcio-tools wheel files"
    echo "DEBUG: Contents of ARTIFACT_DIR after final copy:"
    ls -la "$ARTIFACT_DIR" 2>/dev/null || echo "DEBUG: Failed to list ARTIFACT_DIR"
  fi
else
  echo "DEBUG: ARTIFACT_DIR does not exist: $ARTIFACT_DIR"
fi
