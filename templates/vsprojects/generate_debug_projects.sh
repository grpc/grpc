#!/bin/sh

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

# To properly use this, you'll need to add:
#
#    "debug": true
#
# to build.json

cd `dirname $0`/../..

./tools/buildgen/generate_projects.sh

git diff |
grep \\+Project |
cut -d\" -f 4 |
sort -u |
grep _test$ |
while read p ; do
  mkdir -p templates/vsprojects/$p
  echo '<%namespace file="../vcxproj_defs.include" import="gen_project"/>${gen_project("'$p'", targets)}' > templates/vsprojects/$p/$p.vcxproj.template
done

git diff |
grep \\+Project |
cut -d\" -f 4 |
sort -u |
grep -v _test$ |
while read p ; do
  mkdir -p templates/vsprojects/$p
  echo '<%namespace file="../vcxproj_defs.include" import="gen_project"/>${gen_project("'$p'", libs)}' > templates/vsprojects/$p/$p.vcxproj.template
done

./tools/buildgen/generate_projects.sh
