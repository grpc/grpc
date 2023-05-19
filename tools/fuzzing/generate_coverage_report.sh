#!/bin/bash
# Copyright 2023 The gRPC Authors
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

# This tool builds and runs a fuzzer target and generates coverage report under
# /tmp.
#
# Example:
# Run fuzzer with existing corpus (no fuzzing):
# $ ./tools/fuzzing/generate_coverage_report.sh //test/core/end2end/fuzzers:api_fuzzer test/core/end2end/fuzzers/api_fuzzer_corpus/*
#
# Run with fuzzing:
# $ ./tools/fuzzing/generate_coverage_report.sh //test/core/end2end/fuzzers:api_fuzzer -max_total_time=10 test/core/end2end/fuzzers/api_fuzzer_corpus
#
# Note that if a crash happened during fuzzing, the coverage data will not be dumped.
# See https://github.com/google/fuzzing/issues/41#issuecomment-1027653690 for workaround.

err() {
  echo "$*" >&2
}

if [[ -z "$1" ]]; then
  err "target not specified"
  exit 1
fi

RANDOM=$(date +%s)
FILENAME="${RANDOM}"

export LLVM_PROFILE_FILE=/tmp/"${FILENAME}".profraw
OUTPUT_BASE=$(bazel info output_base)
MIDDLE="execroot/com_github_grpc_grpc/bazel-out/k8-dbg/bin"

CLANG_MAJOR_VERSION=$(clang --version | grep version | sed -r 's/.*version ([^\.]+).*/\1/')
LLVM_PROFDATA="llvm-profdata-${CLANG_MAJOR_VERSION}"
LLVM_COV="llvm-cov-${CLANG_MAJOR_VERSION}"

which "${LLVM_PROFDATA}"
if (( $? != 0 )); then
  err "${LLVM_PROFDATA} not found"
  exit 1
fi

TARGET=$(bazel query "$1")
TARGET_BINARY_PATH="${OUTPUT_BASE}/${MIDDLE}/$(echo ${TARGET:2} | sed 's/:/\//')"

# Build:
bazel build --dynamic_mode=off --config=dbg --config=fuzzer_asan --config=coverage "${TARGET}"
# Run:
"${TARGET_BINARY_PATH}" ${@:2}

if [[ ! -e "${LLVM_PROFILE_FILE}" ]]; then
  err "Profile file ${LLVM_PROFILE_FILE} not created"
  exit 1
fi

# Create coverage report:
"${LLVM_PROFDATA}" merge -sparse "${LLVM_PROFILE_FILE}" -o /tmp/"${FILENAME}".profdata
"${LLVM_COV}" report "${TARGET_BINARY_PATH}" --format=text --instr-profile=/tmp/"${FILENAME}".profdata > /tmp/"${FILENAME}".cov

if (( $? == 0 )); then
  echo "Coverage summary report created: /tmp/${FILENAME}.cov"
  echo "Merged profile data file:        /tmp/${FILENAME}.profdata"
  echo "Raw profile data file:           /tmp/${FILENAME}.profraw"
  echo "There are other ways to explore the data, see https://clang.llvm.org/docs/SourceBasedCodeCoverage.html#creating-coverage-reports"
else
  err "Something went wrong"
  exit 1
fi
