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
#
# This script is invoked by build_docker_and_run_tests.sh inside a docker
# container. You should never need to call this script on your own.

set -e

export CONFIG=$config
export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer
export PATH=$PATH:/usr/bin/llvm-symbolizer

# Ensure that programs depending on current-user-ownership of cache directories
# are satisfied (it's being mounted from outside the image).
chown $(whoami) $XDG_CACHE_HOME

mkdir -p /var/local/git
git clone  /var/local/jenkins/grpc /var/local/git/grpc
# clone gRPC submodules, use data from locally cloned submodules where possible
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')

mkdir -p reports

$POST_GIT_STEP

exit_code=0

$RUN_TESTS_COMMAND || exit_code=$?

cd reports
echo '<html><head></head><body>' > index.html
find . -maxdepth 1 -mindepth 1 -type d | sort | while read d ; do
  d=${d#*/}
  n=${d//_/ }
  echo "<a href='$d/index.html'>$n</a><br />" >> index.html
done
echo '</body></html>' >> index.html
cd ..

zip -r reports.zip reports
find . -name report.xml | xargs -r zip reports.zip
find . -name sponge_log.xml | xargs -r zip reports.zip
find . -name 'report_*.xml' | xargs -r zip reports.zip

exit $exit_code
