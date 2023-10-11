#!/bin/bash
set -ex

cd $(dirname $0)

if [[ $GRPC_RUBY_DEBUG_SYMBOLS_DIR == "" ]]; then
  exit 1
fi
SYMBOLS_DIR=$1
OUTPUT_DIR=$2
PLATFORM=$(basename $SYMBOLS_DIR)
# TODO: selectively copy files
tmp=$(mktemp -d)
cp grpc-native-debug.gemspec $tmp
cp version.rb $tmp
mkdir -p "${tmp}/symbols"
cp -r "${SYMBOLS_DIR}/** ${tmp}/symbols"
# TODO: fix this to use sed
echo $PLATFORM > ./platform.rb
cd $tmp && gem build grpc-native-debug.gem && cp "grpc-native-debug-${PLATFORM}.gem" $OUTPUT_DIR
