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

set -ex

cd $(dirname $0)/../../..

BINS="sync_unary_ping_pong_test sync_streaming_ping_pong_test"
CPUS=`python -c 'import multiprocessing; print multiprocessing.cpu_count()'`

make CONFIG=basicprof -j$CPUS $BINS

mkdir -p reports

# try to use pypy for generating reports
# each trace dumps 7-8gig of text to disk, and processing this into a report is
# heavyweight - so any speed boost is worthwhile
# TODO(ctiller): consider rewriting report generation in C++ for performance
if which pypy >/dev/null; then
  PYTHON=pypy
else
  PYTHON=python2.7
fi

# start processes, interleaving report index generation
echo '<html><head></head><body>' > reports/index.html
for bin in $BINS
do
  bins/basicprof/$bin
  mv latency_trace.txt $bin.trace
  echo "<a href='$bin.txt'>$bin</a><br/>" >> reports/index.html
done
pids=""
# generate report pages... this will take some time
# run them in parallel: they take 1 cpu each
for bin in $BINS
do
  $PYTHON tools/profiling/latency_profile/profile_analyzer.py \
    --source=$bin.trace --fmt=simple > reports/$bin.txt &
  pids+=" $!"
done
echo '</body></html>' >> reports/index.html

# make sure we kill the report generation if something goes wrong
trap "kill $pids || true" 0

# finally, wait for the background report generation to finish
for pid in $pids
do
	if wait $pid
	then
		echo "Finished $pid"
	else
		exit 1
	fi
done
