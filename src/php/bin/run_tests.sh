#!/bin/bash
# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Loads the local shared library, and runs all of the test cases in tests/
# against it
set -ex
cd $(dirname $0)/../../..
root=$(pwd)
cd src/php/bin
source ./determine_extension_dir.sh
# in some jenkins macos machine, somehow the PHP build script can't find libgrpc.dylib
export DYLD_LIBRARY_PATH=$root/libs/$CONFIG
php $extension_dir -d max_execution_time=300 $(which phpunit) -v --debug \
  --exclude-group persistent_list_bound_tests ../tests/unit_tests

php $extension_dir -d max_execution_time=300 $(which phpunit) -v --debug \
  ../tests/unit_tests/PersistentChannelTests

if [ "$1" = "with-php-fpm-tests" ]; then
  if [ -e "/usr/local/etc/php.ini" ]; then
    # start nginx
    service nginx start
    # restart php-fpm
    pkill -o php-fpm
    php-fpm -c /usr/local/etc
    cp ../tests/php-fpm-tests/channel1.php /var/www/html/channel1.php
    cp ../tests/php-fpm-tests/channel2.php /var/www/html/channel2.php
    curl -o - "localhost/channel1.php"
    curl -o - "localhost/channel2.php" | tee output
    res=`head -n 1 output`
    if [ $res == "0" ]; then
      exit 0
    fi
    exit 1
  fi
fi

