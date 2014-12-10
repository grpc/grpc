# Runs the generated code test against the ruby server
cd $(dirname $0)
GRPC_TEST_HOST=localhost:7070 php -d extension_dir=../ext/grpc/modules/ \
  -d extension=grpc.so /usr/local/bin/phpunit -v --debug --strict \
  ../tests/generated_code/GeneratedCodeTest.php
