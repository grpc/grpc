#!/bin/sh
# Loads the local shared library, and runs all of the test cases in tests/
# against it
set -ex
cd $(dirname $0)
default_extension_dir=`php -i | grep extension_dir | sed 's/.*=> //g'`

# sym-link in system supplied extensions
for f in $default_extension_dir/*.so
do
  ln -s $f ../ext/grpc/modules/$(basename $f) || true
done

php \
  -d extension_dir=../ext/grpc/modules/ \
  -d extension=grpc.so \
  phpunit -v --debug --strict ../tests/unit_tests
