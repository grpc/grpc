#!/bin/bash

set -ex

cd $(dirname $0)/../../..

BINS="sync_unary_ping_pong_test sync_streaming_ping_pong_test"
CPUS=`python -c 'import multiprocessing; print multiprocessing.cpu_count()'`

make CONFIG=basicprof -j$CPUS $BINS

mkdir -p reports

echo '<html><head></head><body>' > reports/index.html
for bin in $BINS
do
  bins/basicprof/$bin
  mv latency_trace.txt $bin.trace
  echo "<a href='$bin.txt'>$bin</a><br/>" >> reports/index.html
done
for bin in $BINS
do
  tools/profiling/latency_profile/profile_analyzer.py \
    --source=$bin.trace --fmt=simple > reports/$bin.txt &
done
echo '</body></html>' >> reports/index.html

wait

