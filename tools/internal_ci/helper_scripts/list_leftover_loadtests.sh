#!/usr/bin/env bash
# Copyright 2021 The gRPC Authors
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

set -ex

echo "BEGIN Listing leftover tests."

# Find tests that have running pods and are in Errored state.
kubectl get pods --no-headers -o jsonpath='{range .items[*]}{.metadata.ownerReferences[0].name}{" "}{.status.phase}{"\n"}{end}' \
    | grep Running \
    | cut -f1 -d' ' \
    | sort -u \
    | xargs -r kubectl get loadtest --no-headers -o jsonpath='{range .items[*]}{.metadata.name}{" "}{.metadata.annotations.pool}{" "}{.metadata.annotations.scenario}{" "}{.status}{"\n"}{end}' \
    | grep '"state":"Errored"' || true

echo "END Listing leftover tests."
