#!/bin/bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


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
