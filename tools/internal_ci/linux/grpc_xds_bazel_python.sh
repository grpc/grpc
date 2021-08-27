#!/usr/bin/env bash
# Copyright 2017 gRPC authors.
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
#
# Test full Bazel
#
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

capture_test_logs() {
  # based on http://cs/google3/third_party/fhir/kokoro/common.sh?rcl=211854506&l=18
  mkdir -p "$KOKORO_ARTIFACTS_DIR"
  # copy all test.log and test.xml files to the kokoro artifacts directory
  find -L bazel-testlogs -name "test.log" -o -name "test.xml" -exec cp --parents {} "$KOKORO_ARTIFACTS_DIR" \;
  # Rename the copied test.log and test.xml files to sponge_log.log and sponge_log.xml
  find -L "$KOKORO_ARTIFACTS_DIR" -name "test.log" -exec rename 's/test.log/sponge_log.log/' {} \;
  find -L "$KOKORO_ARTIFACTS_DIR" -name "test.xml" -exec rename 's/test.xml/sponge_log.xml/' {} \;
}

trap capture_test_logs EXIT

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc /var/local/git/grpc
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')
cd /var/local/git/grpc/tools/run_tests/xds_k8s_test_driver

bazel test //tests/url_map:all \
  --test_arg="--server_image=gcr.io/grpc-testing/xds-interop/java-server:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf" \
  --test_arg="--client_image=gcr.io/grpc-testing/xds-interop/python-client:bacf9b1281dba2dc073b9dce1e2896b2f37a8fc5" \
  --test_arg="--kube_context=gke_grpc-testing_us-central1-a_interop-test-psm-sec-v2-us-central1-a" \
  --spawn_strategy=local \
  --test_output=error
