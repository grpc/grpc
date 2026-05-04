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

# Installs a given package using choco with some retry attempts in case of
# network errors

# Ensure package to install is provided
if [ $# -eq 0 ]; then
  echo "Error: No package name provided to install with choco."
  exit 1
fi

PACKAGES=$@
shift $#

MAX_RETRIES=4
# Initial delay, will grow exponentially
delay=5
# Initial exit code.
exit_code=1

PS4='+ $(date "+[%H:%M:%S %Z]")\011 '
set -x

echo "Installing '${PACKAGES} 'using 'apt-get'"

apt-get update && \
for i in $(seq 1 $MAX_RETRIES); do \
  echo Running apt-get install...
  if apt-get install -y ${PACKAGES}; then \
    echo "apt-get succeeded on attempt $i."; \
    break; \
  else \
    echo "apt-get failed on attempt $i. Waiting $(($delay**$i)) seconds before retrying..."; \
    sleep $(($delay**$i)); \
    if [ "$i" -eq "$MAX_RETRIES" ]; then \
      echo "Max retries reached ($MAX_RETRIES). apt-get failed permanently."; \
      exit 1; \
    fi \
  fi \
done