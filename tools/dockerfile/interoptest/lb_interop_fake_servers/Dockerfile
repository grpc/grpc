# Copyright 2015 gRPC authors.
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

FROM golang:1.19

#=================
# Install apt-get-install-retry wrapper script
#=================
#  Create wrapper for apt-get

#=================
#  Create wrapper for apt-get

RUN echo '#!/bin/bash' > /usr/local/bin/apt-get-install-retry && \
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
        echo '    if apt-get update && apt-get install -y "${PACKAGES[@]}"; then' >> /usr/local/bin/apt-get-install-retry && \
        echo '    echo "apt-get succeeded on attempt ${i}.";' >> /usr/local/bin/apt-get-install-retry && \
        echo '    break;' >> /usr/local/bin/apt-get-install-retry && \
        echo '  else' >> /usr/local/bin/apt-get-install-retry && \
        echo '    if [ "${i}" -eq "${MAX_RETRIES}" ]; then' >> /usr/local/bin/apt-get-install-retry && \
        echo '      echo "Max retries reached (${MAX_RETRIES}). apt-get failed permanently." >&2' >> /usr/local/bin/apt-get-install-retry && \
        echo '      exit 1;' >> /usr/local/bin/apt-get-install-retry && \
        echo '    fi' >> /usr/local/bin/apt-get-install-retry && \
        echo '    ' >> /usr/local/bin/apt-get-install-retry && \
        echo '    echo "apt-get failed on attempt ${i}. Waiting $((delay**i)) seconds before retrying..."' >> /usr/local/bin/apt-get-install-retry && \
        echo '    sleep $((delay**i));' >> /usr/local/bin/apt-get-install-retry && \
        echo '  fi' >> /usr/local/bin/apt-get-install-retry && \
        echo 'done' >> /usr/local/bin/apt-get-install-retry && \
        chmod +x /usr/local/bin/apt-get-install-retry

RUN apt-get-install-retry \
  dnsutils \
  git \
  vim \
  curl \
  python3 \
  python3-pip \
  python3-yaml \
  make && apt-get clean

RUN ln -s /usr/local/go/bin/go /usr/local/bin

# Install Python packages from PyPI
RUN python3 -m pip install --break-system-packages virtualenv
RUN python3 -m pip install --break-system-packages futures enum34 protobuf six twisted

# Define the default command.
CMD ["bash"]
