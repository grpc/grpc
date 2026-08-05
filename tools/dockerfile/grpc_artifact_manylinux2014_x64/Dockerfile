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

RUN /bin/bash -c 'echo -e "#!/usr/bin/env bash\n\
# Copyright 2026 The gRPC Authors\n\
\n\
PS4=\"+ \$(date +[%H:%M:%S_%Z]) \"\n\
set -xeuo pipefail\n\
\n\
MAX_RETRIES=4\n\
delay=3\n\
\n\
for i in \$(seq 1 \$MAX_RETRIES); do\n\
  if apt-get update && apt-get install -y \"\$@\"; then\n\
    break\n\
  fi\n\
  sleep \$((delay**i))\n\
done\n\
" > /usr/local/bin/apt-get-install-retry'
RUN chmod +x /usr/local/bin/apt-get-install-retry

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
