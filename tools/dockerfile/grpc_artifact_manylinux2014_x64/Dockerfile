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

# Docker file for building gRPC artifacts.
# Updated: 2021-08-23

##################
# Base

FROM dockcross/manylinux2014-x64

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

# Install essential packages.
RUN yum -y install strace libyaml-devel && yum clean all

##################
# Ruby dependencies

# Install rvm
RUN curl -sSL https://rvm.io/mpapis.asc | gpg --import -
RUN curl -sSL https://rvm.io/pkuczynski.asc | gpg --import -
# Use "--insecure" to avoid cert expiration error
RUN curl -sSL --insecure https://get.rvm.io | bash -s stable

# Install Ruby 3.2
RUN ulimit -n 1024 && /bin/bash -l -c "rvm install ruby-3.2.11"
RUN /bin/bash -l -c "rvm use --default ruby-3.2.11"
RUN /bin/bash -l -c "echo 'gem: --no-document' > ~/.gemrc"
RUN /bin/bash -l -c "echo 'export PATH=/usr/local/rvm/bin:$PATH' >> ~/.bashrc"
RUN /bin/bash -l -c "echo 'rvm --default use ruby-3.2.11' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install bundler"

# Create default work directory.
RUN mkdir /var/local/jenkins

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always

# Define the default command.
CMD ["bash"]
