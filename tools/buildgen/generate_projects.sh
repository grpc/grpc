#!/bin/bash

set -ex

if [ "x$TEST" == "x" ] ; then
  TEST=false
fi


cd `dirname $0`/..
mako_renderer=tools/buildgen/mako_renderer.py
gen_build_json=test/core/end2end/gen_build_json.py

end2end_test_build=`mktemp`
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
      out=`mktemp`
    else
      g4 open $out || true
    fi
    $mako_renderer $plugins $data -o $out $file
    if [ $TEST == true ] ; then
      diff -q $out $actual_out
      rm $out
    fi
  done
done

rm $end2end_test_build

