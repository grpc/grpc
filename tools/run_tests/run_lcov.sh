#!/bin/bash

set -ex

out=`realpath ${1:-coverage}`

root=`realpath $(dirname $0)/../..`
tmp=`mktemp`
cd $root
tools/run_tests/run_tests.py -c gcov -l c c++
lcov --capture --directory . --output-file $tmp
genhtml $tmp --output-directory $out
rm $tmp
if which xdg-open > /dev/null
then
  xdg-open file://$out/index.html
fi

