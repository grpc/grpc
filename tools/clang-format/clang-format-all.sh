#!/bin/bash
set -ex
cd $(dirname $0)/../..
for dir in src test include
do
  find $dir -name '*.c' -or -name '*.cc' -or -name '*.h' | xargs clang-format -i
done

