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

if [ "x$TEST" == "x" ] ; then
  TEST=false
fi


cd `dirname $0`/../..
mako_renderer=tools/buildgen/mako_renderer.py
gen_build_json=test/core/end2end/gen_build_json.py

tools/buildgen/build-cleaner.py build.json

end2end_test_build=`mktemp /tmp/genXXXXXX`
$gen_build_json > $end2end_test_build

global_plugins=`find ./tools/buildgen/plugins -name '*.py' |
  sort | grep -v __init__ |
  while read p ; do echo -n "-p $p " ; done`

for dir in . ; do
  local_plugins=`find $dir/templates -name '*.py' |
    sort | grep -v __init__ |
    while read p ; do echo -n "-p $p " ; done`

  plugins="$global_plugins $local_plugins"

  find -L $dir/templates -type f -and -name *.template | while read file ; do
    out=${dir}/${file#$dir/templates/}  # strip templates dir prefix
    out=${out%.*}  # strip template extension
    json_files="build.json $end2end_test_build"
    data=`for i in $json_files; do echo -n "-d $i "; done`
    if [ $TEST == true ] ; then
      actual_out=$out
      out=`mktemp /tmp/gentXXXXXX`
    fi
    $mako_renderer $plugins $data -o $out $file
    if [ $TEST == true ] ; then
      diff -q $out $actual_out
      rm $out
    fi
  done
done

rm $end2end_test_build
