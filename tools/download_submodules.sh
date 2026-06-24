#!/usr/bin/env bash

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

set -euo pipefail

# This script runs during ./configure to download missing C/C++ dependencies (submodules)
# using standard git clone and checkout commands.

SRCDIR="$(cd "$(dirname "$0")/.." && pwd)"
METADATA_FILE="$SRCDIR/submodules_metadata.txt"

if [ ! -f "$METADATA_FILE" ]; then
    echo "Error: Submodules metadata file not found at $METADATA_FILE"
    exit 1
fi

echo "=== Checking C/C++ dependencies (submodules) ==="

while read -r path url commit; do
    # Skip empty lines or comments
    if [ -z "$path" ] || [[ "$path" =~ ^# ]]; then
        continue
    fi
    
    target_dir="$SRCDIR/$path"
    
    # Check if the submodule directory is already populated
    if [ -d "$target_dir" ] && [ "$(ls -A "$target_dir" 2>/dev/null)" ]; then
        echo "Dependency $path is already present. Skipping download."
        continue
    fi
    
    echo "Dependency $path is missing or empty."
    echo "Cloning $path from $url at commit $commit..."
    
    # Create parent directories if they don't exist
    mkdir -p "$(dirname "$target_dir")"
    
    # Clone the repository
    git clone "$url" "$target_dir"
    
    # Checkout the specific commit
    (
        cd "$target_dir"
        git checkout --quiet "$commit"
    )
    
    echo "Successfully configured $path!"
done < "$METADATA_FILE"

echo "=== All C/C++ dependencies configured successfully ==="