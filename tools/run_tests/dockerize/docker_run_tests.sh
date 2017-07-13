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
