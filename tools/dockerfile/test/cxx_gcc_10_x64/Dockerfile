# Copyright 2026 gRPC authors.
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

FROM gcc:10

# Install apt-get-install-retry wrapper script
RUN echo '#!/usr/bin/env bash' > /usr/local/bin/apt-get-install-retry && \
    echo '# Copyright 2026 The gRPC Authors' >> /usr/local/bin/apt-get-install-retry && \
    echo '#' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Licensed under the Apache License, Version 2.0 (the "License");' >> /usr/local/bin/apt-get-install-retry && \
    echo '# you may not use this file except in compliance with the License.' >> /usr/local/bin/apt-get-install-retry && \
    echo '# You may obtain a copy of the License at' >> /usr/local/bin/apt-get-install-retry && \
    echo '#' >> /usr/local/bin/apt-get-install-retry && \
    echo '#     http://www.apache.org/licenses/LICENSE-2.0' >> /usr/local/bin/apt-get-install-retry && \
    echo '#' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Unless required by applicable law or agreed to in writing, software' >> /usr/local/bin/apt-get-install-retry && \
    echo '# distributed under the License is distributed on an "AS IS" BASIS,' >> /usr/local/bin/apt-get-install-retry && \
    echo '# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.' >> /usr/local/bin/apt-get-install-retry && \
    echo '# See the License for the specific language governing permissions and' >> /usr/local/bin/apt-get-install-retry && \
    echo '# limitations under the License.' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Ensure all tracking is registered' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'PS4="+ \$(date +[%H:%M:%S_%Z]) "' >> /usr/local/bin/apt-get-install-retry && \
    echo 'set -xeuo pipefail' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Ensure package to install is provided' >> /usr/local/bin/apt-get-install-retry && \
    echo 'if [ $# -eq 0 ]; then' >> /usr/local/bin/apt-get-install-retry && \
    echo '  echo "Error: No package name provided to install with apt-get." >&2' >> /usr/local/bin/apt-get-install-retry && \
    echo '  exit 1' >> /usr/local/bin/apt-get-install-retry && \
    echo 'fi' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'PACKAGES=("$@")' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'MAX_RETRIES=4' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Initial delay, will grow exponentially' >> /usr/local/bin/apt-get-install-retry && \
    echo 'delay=3' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'echo "Installing packages ${PACKAGES[*]} using apt-get"' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'for i in $(seq 1 $MAX_RETRIES); do' >> /usr/local/bin/apt-get-install-retry && \
    echo '  echo "Running apt-get update and install (attempt ${i})..."' >> /usr/local/bin/apt-get-install-retry && \
    echo '  if DEBIAN_FRONTEND=noninteractive apt-get update -qq && \' >> /usr/local/bin/apt-get-install-retry && \
    echo '     DEBIAN_FRONTEND=noninteractive apt-get install -qq -y \' >> /usr/local/bin/apt-get-install-retry && \
    echo '     -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" \' >> /usr/local/bin/apt-get-install-retry && \
    echo '      "${PACKAGES[@]}"; then' >> /usr/local/bin/apt-get-install-retry && \
    echo '    echo "apt-get succeeded on attempt ${i}."' >> /usr/local/bin/apt-get-install-retry && \
    echo '    break' >> /usr/local/bin/apt-get-install-retry && \
    echo '  else' >> /usr/local/bin/apt-get-install-retry && \
    echo '    if [ "${i}" -eq "${MAX_RETRIES}" ]; then' >> /usr/local/bin/apt-get-install-retry && \
    echo '      echo "Max retries reached (${MAX_RETRIES}). apt-get failed permanently." >&2' >> /usr/local/bin/apt-get-install-retry && \
    echo '      exit 1' >> /usr/local/bin/apt-get-install-retry && \
    echo '    fi' >> /usr/local/bin/apt-get-install-retry && \
    echo '    ' >> /usr/local/bin/apt-get-install-retry && \
    echo '    echo "apt-get failed on attempt ${i}. Waiting $((delay**i)) seconds before retrying..."' >> /usr/local/bin/apt-get-install-retry && \
    echo '    sleep $((delay**i))' >> /usr/local/bin/apt-get-install-retry && \
    echo '  fi' >> /usr/local/bin/apt-get-install-retry && \
    echo 'done' >> /usr/local/bin/apt-get-install-retry && \
    chmod +x /usr/local/bin/apt-get-install-retry





RUN apt-get-install-retry curl git time zip

#=================
# Setup git to access working directory across docker boundary.
# This avoids the "fatal: detected dubious ownership in repository XYZ"
# git error.

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always

#====================
# run_tests.py python dependencies

# Basic python dependencies to be able to run tools/run_tests python scripts
# These dependencies are not sufficient to build gRPC Python, gRPC Python
# deps are defined elsewhere (e.g. python_deps.include)
RUN apt-get-install-retry \
  python3 \
  python3-pip \
  python3-setuptools \
  python3-yaml && apt-get clean
 

# use pinned version of pip to avoid sudden breakages
RUN python3 -m pip install --upgrade pip==19.3.1

# TODO(jtattermusch): currently six is needed for tools/run_tests scripts
# but since our python2 usage is deprecated, we should get rid of it.
RUN python3 -m pip install six==1.16.0

# Google Cloud Platform API libraries
# These are needed for uploading test results to BigQuery (e.g. by tools/run_tests scripts)
RUN python3 -m pip install --upgrade google-auth==1.23.0 google-api-python-client==1.12.8 oauth2client==4.1.0


# Some cxx tests depend on the twisted package
RUN python3 -m pip install twisted

#=================
# Install cmake
# Note that this step should be only used for distributions that have new enough cmake to satisfy gRPC's cmake version requirement.

RUN apt-get-install-retry cmake


# Install ccache 4.2 from official repo.
RUN apt-get-install-retry ccache && apt-get clean


RUN mkdir /var/local/jenkins


# Define the default command.
CMD ["bash"]
