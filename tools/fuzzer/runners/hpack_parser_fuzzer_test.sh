#!/bin/bash
# Copyright 2016 gRPC authors.
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

flags="-max_total_time=$runtime -artifact_prefix=fuzzer_output/ -max_len=512 -timeout=120"

flags="$flags -dict=test/core/end2end/fuzzers/hpack.dictionary"

if [ "$jobs" != "1" ]
then
  flags="-jobs=$jobs -workers=$jobs $flags"
fi

if [ "$config" == "asan-trace-cmp" ]
then
  flags="-use_traces=1 $flags"
fi

bins/$config/hpack_parser_fuzzer_test $flags fuzzer_output test/core/transport/chttp2/hpack_parser_corpus
