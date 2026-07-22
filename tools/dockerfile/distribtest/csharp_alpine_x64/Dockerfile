# Copyright 2019 The gRPC Authors
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

FROM mcr.microsoft.com/dotnet/core/sdk:2.1-alpine3.9

#=================
# Install apt-get-install-retry wrapper script
#=================
#  Create wrapper for apt-get

COPY --chmod=+x <<-"EOF" /usr/local/bin/apt-get-install-retry
#!/usr/bin/env bash
# Copyright 2026 The gRPC Authors
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


# Ensure all tracking are registered

PS4="+ \$(date +[%H:%M:%S_%Z]) "
set -xeuo pipefail

# Ensure package to install is provided
if [ $# -eq 0 ]; then
  echo "Error: No package name provided to install with apt-get." >&2
  exit 1
fi

PACKAGES=("$@")

MAX_RETRIES=4
# Initial delay, will grow exponentially
delay=3

echo "Installing packages ${PACKAGES} using apt-get"

for i in $(seq 1 $MAX_RETRIES); do
  echo "Running apt-get update and install (attempt ${i})..."
  if apt-get update && apt-get install -y "${PACKAGES[@]}"; then
    echo "apt-get succeeded on attempt ${i}.";
    break;
  else
    if [ "${i}" -eq "${MAX_RETRIES}" ]; then
      echo "Max retries reached (${MAX_RETRIES}). apt-get failed permanently." >&2
      exit 1;
    fi
    
    echo "apt-get failed on attempt ${i}. Waiting $(($delay**$i)) seconds before retrying..."
    sleep $(($delay**$i));
  fi
done

EOF


RUN apk update && apk add bash
RUN apk update && apk add unzip

# Workaround for https://github.com/grpc/grpc/issues/18428
# Also see https://github.com/sgerrand/alpine-pkg-glibc
RUN apk update && apk --no-cache add ca-certificates wget
RUN wget -q -O /etc/apk/keys/sgerrand.rsa.pub https://alpine-pkgs.sgerrand.com/sgerrand.rsa.pub
RUN wget https://github.com/sgerrand/alpine-pkg-glibc/releases/download/2.30-r0/glibc-2.30-r0.apk
RUN apk add glibc-2.30-r0.apk

# installing mono on alpine is hard and we don't really need it
ENV SKIP_NET45_DISTRIBTEST=1
# we have a separate distribtest for netcoreapp3.1
ENV SKIP_NETCOREAPP31_DISTRIBTEST=1
# we have a separate distribtest for net5.0
ENV SKIP_NET50_DISTRIBTEST=1
# we have a separate distribtest for net7.0
ENV SKIP_NET70_DISTRIBTEST=1
