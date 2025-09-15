#!/bin/sh

set -eu

echo "=> TEST: extension not loaded"
timeout 5 php fork.php
echo "=> PASS"
echo ""

echo "=> TEST: extension loaded, default configuration"
timeout 5 php -d extension=grpc.so fork.php
echo "=> PASS"
echo ""

echo "=> TEST: extension loaded, fork support enabled"
timeout 5 php -d extension=grpc.so -d grpc.enable_fork_support=1 fork.php
echo "=> PASS"
echo ""

echo "=> TEST: extension loaded, fork support enabled, poll strategy set"
timeout 5 php -d extension=grpc.so -d grpc.enable_fork_support=1 -d grpc.poll_strategy=epoll1 fork.php
echo "=> PASS"
echo ""
