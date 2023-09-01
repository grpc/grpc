#!/bin/bash
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


set -e

cd `dirname $0`/../..

function process_dir {
  base_dir=$1
  prefix=$2
  comment_language=$3
  (
    cd $base_dir
    find . -name "*.h" | while read f ; do
      guard=${prefix}_`echo ${f#*/} | tr '[:lower:]/.-' '[:upper:]___'`
      if [ "$comment_language" = "c++" ] ; then
        comment="// $guard"
      else
        comment="/* $guard */"
      fi
      awk '
        BEGIN {
          guard = "'${guard}'";
          comment_language = "'${comment_language}'";
        }
        prev ~ /^#ifndef/ && !got_first_ifndef {
          got_first_ifndef = 1;
          prev = "#ifndef " guard;
        }
        prev ~ /^#define/ && !got_first_define {
          got_first_define = 1;
          prev = "#define " guard;
        }
        NR > 1 { print prev; }
        { prev = $0; }
        END {
          if (prev ~ /^#endif/) {
            if (comment_language ~ /^c$/) {
              print "#endif  /* " guard " */";
            } else if (comment_language ~ /^c\+\+$/) {
              print "#endif  // " guard;
            } else {
              print "ERROR: unknown comment language: " comment_language;
            }
          } else {
            print "ERROR: file does not end with #endif";
          }
        }
      ' "${f}" > "${f}.rewritten"
      mv "${f}.rewritten" "${f}"
    done
  )
}

process_dir include/grpc GRPC c
process_dir include/grpc++ GRPCXX c++
process_dir src/core GRPC_INTERNAL_CORE c
process_dir src/cpp GRPC_INTERNAL_CPP c++
process_dir src/compiler GRPC_INTERNAL_COMPILER c++
process_dir test/core GRPC_TEST_CORE c
process_dir test/cpp GRPC_TEST_CPP c++
process_dir examples GRPC_EXAMPLES c++
