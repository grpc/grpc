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

FROM grpc/clang:latest

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
RUN chmod +x /usr/local/bin/apt-get-install-retryRUN apt-get-install-retry \
  autoconf \
  libtool \
  libgtest-dev \
  && apt-get clean

RUN git clone --recursive https://github.com/grpc/grpc.git

EXPOSE 8181

CMD \
  (cd grpc ; git pull) && \
  (cd grpc ; git submodule update --init --recursive) && \
  llvm/tools/clang/tools/scan-build/scan-build -o /tmp/grpc --use-analyzer=/usr/local/bin/clang make -C grpc buildtests && \
  llvm/tools/clang/tools/scan-view/scan-view /tmp/grpc/`ls /tmp/grpc` --host 0.0.0.0 --no-browser --allow-all-hosts
