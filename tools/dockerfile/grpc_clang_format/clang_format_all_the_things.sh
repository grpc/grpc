#!/bin/bash

# directories to run against
DIRS="src/core src/cpp test/core test/cpp include"

# file matching patterns to check
GLOB="*.h *.c *.cc"

# clang format command
CLANG_FORMAT=clang-format-3.6

files=
for dir in $DIRS
do
  for glob in $GLOB
  do
    files="$files `find /local-code/$dir -name $glob`"
  done
done

if [ "x$TEST" = "x" ]
then
  echo $files | xargs $CLANG_FORMAT -i
else
  for file in $files
  do
    $CLANG_FORMAT $file | diff $file -
  done
fi

