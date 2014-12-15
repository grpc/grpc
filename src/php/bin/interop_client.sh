#!/bin/sh

set +e
cd $(dirname $0)
php -d extension_dir=../ext/grpc/modules/ -d extension=grpc.so \
  ../tests/interop/interop_client.php $@ 1>&2
