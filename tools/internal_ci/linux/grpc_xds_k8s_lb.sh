#!/usr/bin/env bash
# Copyright 2021 gRPC authors.
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

set -eo pipefail

script_dir="$(dirname "$0")"

run_test() {
  local test_name="${1:?Usage: run_test test_name}"
  echo "${test_name}"

  local script="good.py"
  if [[ "$test_name" == "failover_test" ]]; then
    script="bad.py"
  fi
  #  set -x
  python3 "${script_dir}/${script}" |& tee "sponge_log.log"
}


main() {
#  set -x
  bash --version
  echo ""

  # Run tests
  local failed_tests=0
  test_suites=("api_listener_test" "change_backend_service_test" "failover_test" "remove_neg_test" "round_robin_test" "affinity_test" "outlier_detection_test")
  for test in "${test_suites[@]}"; do
    # run_test $test || (( failed_tests++ ))
    run_test $test ||  (( failed_tests++ )) && true
    echo
  done
  echo
  echo "Failed test suites: ${failed_tests}"
  if (( failed_tests > 0 )); then
    exit 1
  fi
}

main "$@"
