#!/bin/bash

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
