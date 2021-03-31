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

set -ex

rm -rf ~/.rake-compiler

CROSS_RUBY="$(pwd)/$(mktemp tmpfile.XXXXXXXX)"

curl https://raw.githubusercontent.com/rake-compiler/rake-compiler/v1.1.1/tasks/bin/cross-ruby.rake > "$CROSS_RUBY"

# See https://github.com/grpc/grpc/issues/12161 for verconf.h patch details
patch "$CROSS_RUBY" << EOF
--- cross-ruby.rake	2021-03-05 12:04:09.898286632 -0800
+++ patched	2021-03-05 12:05:35.594318962 -0800
@@ -111,10 +111,11 @@
     "--host=#{MINGW_HOST}",
     "--target=#{MINGW_TARGET}",
     "--build=#{RUBY_BUILD}",
-    '--enable-shared',
+    '--enable-static',
+    '--disable-shared',
     '--disable-install-doc',
+    '--without-gmp',
     '--with-ext=',
-    'LDFLAGS=-pipe -s',
   ]
 
   # Force Winsock2 for Ruby 1.8, 1.9 defaults to it
@@ -130,6 +131,7 @@
 # make
 file "#{build_dir}/ruby.exe" => ["#{build_dir}/Makefile"] do |t|
   chdir File.dirname(t.prerequisites.first) do
+    sh "test -s verconf.h || rm -f verconf.h"  # if verconf.h has size 0, make sure it gets re-built by make
     sh MAKE
   end
 end
EOF

MAKE="make -j8"

# Install ruby 3.0.0 for rake-compiler
# Download ruby 3.0.0 sources outside of the cross-ruby.rake file, since the
# latest rake-compiler/v1.1.1 cross-ruby.rake file requires tar.bz2 source
# files.
# TODO(apolcyn): remove this hack when tar.bz2 sources are available for ruby
# 3.0.0 in https://ftp.ruby-lang.org/pub/ruby/3.0/. Also see
# https://stackoverflow.com/questions/65477613/rvm-where-is-ruby-3-0-0.
set +x # rvm commands are very verbose
source ~/.rvm/scripts/rvm
echo "rvm use 3.0.0"
rvm use 3.0.0
set -x
RUBY_3_0_0_TAR="${HOME}/.rake-compiler/sources/ruby-3.0.0.tar.gz"
mkdir -p "$(dirname $RUBY_3_0_0_TAR)"
curl -L "https://ftp.ruby-lang.org/pub/ruby/3.0/$(basename $RUBY_3_0_0_TAR)" -o "$RUBY_3_0_0_TAR"
ccache -c
ruby --version | grep 'ruby 3.0.0'
rake -f "$CROSS_RUBY" cross-ruby VERSION=3.0.0 HOST=x86_64-darwin11 MAKE="$MAKE" SOURCE="$RUBY_3_0_0_TAR"
echo "installed ruby 3.0.0 build targets"
# Install ruby 2.7.0 for rake-compiler
set +x
echo "rvm use 2.7.0"
rvm use 2.7.0
set -x
ruby --version | grep 'ruby 2.7.0'
ccache -c
rake -f "$CROSS_RUBY" cross-ruby VERSION=2.7.0 HOST=x86_64-darwin11 MAKE="$MAKE"
echo "installed ruby 2.7.0 build targets"
# Install ruby 2.4-2.6 for rake-compiler
set +x
echo "rvm use 2.5.0"
rvm use 2.5.0
set -x
ruby --version | grep 'ruby 2.5.0'
for v in 2.6.0 2.5.0 2.4.0 ; do
  ccache -c
  rake -f "$CROSS_RUBY" cross-ruby VERSION="$v" HOST=x86_64-darwin11 MAKE="$MAKE"
  echo "installed ruby $v build targets"
done

sed 's/x86_64-darwin-11/universal-darwin/' ~/.rake-compiler/config.yml > "$CROSS_RUBY"
mv "$CROSS_RUBY" ~/.rake-compiler/config.yml
