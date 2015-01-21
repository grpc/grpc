#!/bin/bash
set -e
source $(dirname $0)/config.sh
cd $(dirname $0)/../..
for dir in src test include
do
  find $dir -name '*.c' -or -name '*.cc' -or -name '*.h' | xargs $CLANG_FORMAT -i
done

