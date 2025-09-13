#!/bin/bash
# Copyright 2022 The gRPC Authors
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

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# change to grpc repo root
cd $(dirname $0)/../../..

export PREPARE_BUILD_INSTALL_DEPS_PYTHON=true
source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# TODO(jtattermusch): cleanup this prepare build step (needed for python artifact build)
# install cython for all python versions
# Install/update uv using standalone script for latest version with aarch64 musl support
echo "Installing/updating uv using standalone script for latest version with aarch64 musl support"

# First, try to install uv using the standalone script
echo "Running standalone installation script..."
if curl -LsSf https://astral.sh/uv/install.sh | sh; then
  echo "Standalone installation script completed successfully"
else
  echo "Standalone installation script failed, trying pip installation as fallback..."
  # Try to install uv via pip as fallback
  if python3 -m pip install --user uv; then
    echo "Successfully installed uv via pip"
  else
    echo "Failed to install uv via pip as well"
  fi
fi

echo "DEBUG: UV installation phase completed, continuing with PATH setup..."

# Add multiple possible uv installation paths to PATH
export PATH="$HOME/.local/bin:$HOME/.cargo/bin:/usr/local/bin:/opt/homebrew/bin:$PATH"

# Also try to source cargo env as fallback (but don't fail if it doesn't exist)
if [ -f "$HOME/.cargo/env" ]; then
  echo "DEBUG: Sourcing cargo env from $HOME/.cargo/env"
  source "$HOME/.cargo/env" 2>/dev/null || echo "DEBUG: Failed to source cargo env, continuing anyway"
else
  echo "DEBUG: Cargo env file not found at $HOME/.cargo/env, skipping"
fi

# Debug: Check if uv is available immediately after PATH update
echo "DEBUG: Checking if uv is available after PATH update..."
if command -v uv >/dev/null 2>&1; then
  echo "DEBUG: uv is available immediately after PATH update"
  echo "DEBUG: uv version: $(uv --version)"
else
  echo "DEBUG: uv not available immediately after PATH update, will check installation locations"
fi

# Check common installation locations for uv
UV_PATHS=("$HOME/.local/bin/uv" "$HOME/.cargo/bin/uv" "/usr/local/bin/uv" "/opt/homebrew/bin/uv")
UV_FOUND=""

echo "Checking for uv in common installation locations..."
for uv_path in "${UV_PATHS[@]}"; do
  if [ -f "$uv_path" ]; then
    echo "Found uv at: $uv_path"
    export PATH="$(dirname "$uv_path"):$PATH"
    UV_FOUND="$uv_path"
    break
  fi
done

if command -v uv >/dev/null 2>&1; then
    echo "Successfully installed/updated uv to latest version for Mac distribtests"
    echo "uv version: $(uv --version)"
    # Install for all Python versions using uv
    for py_version in python3.9 python3.10 python3.11 python3.12 python3.13 python3.14; do
      if command -v "$py_version" >/dev/null 2>&1; then
        echo "Installing packages for $py_version using uv"
        if [[ "$py_version" == "python3.12" || "$py_version" == "python3.13" || "$py_version" == "python3.14" ]]; then
          # Use --break-system-packages for newer Python versions
          uv pip install --system --python "$py_version" -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user --break-system-packages
        else
          uv pip install --system --python "$py_version" -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user
        fi
      else
        echo "Warning: $py_version not found, skipping"
      fi
    done
  else
    echo "uv installation failed - command not found after installation"
    echo "PATH: $PATH"
    echo "Checking common installation locations:"
    for uv_path in "${UV_PATHS[@]}"; do
      echo "Checking $uv_path:"
      ls -la "$uv_path" 2>/dev/null || echo "  Not found"
    done
    echo "Falling back to pip for package installation"
    echo "DEBUG: Using pip for all Python package installations"
    python3.9 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user
    python3.10 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user
    python3.11 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user
    python3.12 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user --break-system-packages
    python3.13 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user --break-system-packages
    python3.14 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user --break-system-packages
  fi
else
  echo "Failed to install uv, using pip for package installation"
  python3.9 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user
  python3.10 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user
  python3.11 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user
  python3.12 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user --break-system-packages
  python3.13 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user --break-system-packages
  python3.14 -m pip install -U 'cython==3.1.1' setuptools==65.4.1 six==1.16.0 wheel --user --break-system-packages
fi

# Build all python macos artifacts (this step actually builds all the binary wheels and source archives)
tools/run_tests/task_runner.py -f artifact macos python ${TASK_RUNNER_EXTRA_FILTERS} -j 2 -x build_artifacts/sponge_log.xml || FAILED="true"

# the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/* input_artifacts/ || true

# Collect the python artifact from subdirectories of input_artifacts/ to artifacts/
# TODO(jtattermusch): when collecting the artifacts that will later be uploaded as kokoro job artifacts,
# potentially skip some file names that would clash with linux-created artifacts.
cp -r input_artifacts/python_*/* artifacts/ || true

# TODO(jtattermusch): Here we would normally run python macos distribtests, but currently no such tests are defined
# in distribtest_targets.py

# This step checks if any of the artifacts exceeds a per-file size limit.
tools/internal_ci/helper_scripts/check_python_artifacts_size.sh

tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if [ "$FAILED" != "" ]
then
  exit 1
fi
