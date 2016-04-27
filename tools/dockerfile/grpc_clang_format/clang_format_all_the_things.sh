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

# directories to run against
DIRS="src/core/lib src/core/ext src/cpp test/core test/cpp include"

# file matching patterns to check
GLOB="*.h *.c *.cc"

# clang format command
CLANG_FORMAT=clang-format-3.8

files=
for dir in $DIRS
do
  for glob in $GLOB
  do
    files="$files `find /local-code/$dir -name $glob -and -not -name *.generated.* -and -not -name *.pb.h -and -not -name *.pb.c`"
  done
done

# The CHANGED_FILES variable is used to restrict the set of files to check.
# Here we set files to the intersection of files and CHANGED_FILES
if [ -n "$CHANGED_FILES" ]; then
  files=$(comm -12 <(echo $files | tr ' ' '\n' | sort -u) <(echo $CHANGED_FILES | tr ' ' '\n' | sort -u))
fi

if [ "x$TEST" = "x" ]
then
  echo $files | xargs $CLANG_FORMAT -i
else
  ok=yes
  for file in $files
  do
    tmp=`mktemp`
    $CLANG_FORMAT $file > $tmp
    diff -u $file $tmp || ok=no
    rm $tmp
  done
  if [ $ok == no ]
  then
    false
  fi
fi
