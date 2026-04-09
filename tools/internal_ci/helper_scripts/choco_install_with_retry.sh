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
if [ -z "$1" ]; then
    echo "Error: No package name provided to install with choco."
    exit 1
fi

PACKAGE=$1
shift
INSTALL_FLAGS="$@"
MAX_RETRIES=3

echo "Installing $PACKAGE using 'choco'"

for ((i=1; i<=MAX_RETRIES; i++)); do
    choco install "$PACKAGE" -y "$INSTALL_FLAGS" && exit 0

    if [ $i -lt $MAX_RETRIES ]; then
      echo "Attempt $i to install $PACKAGE failed. Retrying..."
      sleep 3
done

echo "All attempts to install $PACKAGE failed. Exiting..."
exit 1
