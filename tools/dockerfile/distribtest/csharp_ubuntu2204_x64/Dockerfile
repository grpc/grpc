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

FROM ubuntu:22.04
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



RUN apt update && apt -y upgrade
RUN apt-get-install-retry apt-transport-https ca-certificates gnupg wget unzip && apt clean

# Install dotnet SDK
RUN wget https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb -O packages-microsoft-prod.deb \
    && dpkg -i packages-microsoft-prod.deb \
    && rm packages-microsoft-prod.deb \
    && apt-get-install-retry dotnet-sdk-8.0 \
    && apt clean

# we only want to test dotnet core runtime. This also allows us to keep
# this docker image minimal by not installing the other runtimes.
ENV SKIP_NET45_DISTRIBTEST=1
ENV SKIP_NETCOREAPP21_DISTRIBTEST=1
ENV SKIP_NETCOREAPP31_DISTRIBTEST=1
ENV SKIP_NET50_DISTRIBTEST=1
