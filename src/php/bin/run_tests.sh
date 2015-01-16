#!/bin/sh
# Loads the local shared library, and runs all of the test cases in tests/
# against it
cd $(dirname $0)
php -d extension_dir=../ext/grpc/modules/ -d extension=grpc.so \
  /usr/local/bin/phpunit -v --debug --strict ../tests/unit_tests
