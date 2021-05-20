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
# BEGIN ignore errors.
set +e

# Find tests that have running pods and are in Errored state.
kubectl get pods --no-headers | grep -v Completed | cut -f1 -d' ' \
    | (while read f;  do echo $(kubectl get pod "$f" --no-headers -o jsonpath='{.metadata.ownerReferences[0].name}'); done) \
    | (while read f; do state=$(kubectl get loadtest "$f" --no-headers -o  jsonpath='{.status.state}'); if [[ "${state}" == Errored ]]; then echo "$f"; fi; done) \
    | sort > leftovertests.txt

# List annotations of tests that have running pods and are in Errored state.
# Expression is intended to work across versions of kubectl.
cat leftovertests.txt \
    | xargs -r kubectl get loadtest --no-headers \
    | (while read -a words; do annotations=(
        $(kubectl get loadtest "${words[0]}" --no-headers -o jsonpath='{.metadata.annotations.pool}{" "}{.metadata.annotations.scenario}{" "}{.metadata.annotations.uniquifier}')
        ); echo "${words[1]} {\"pool\":\"${annotations[0]}\",\"scenario\":\"${annotations[1]}\",\"uniquifier\":\"${annotations[2]}\"}"; done) \
    | sort

# END ignore errors.
set -e

# Delete tests that have running pods and are in Errored state.
# This is a workaround for the accumulation of leftover tests.
uniq leftovertests.txt | xargs -r kubectl delete loadtest
