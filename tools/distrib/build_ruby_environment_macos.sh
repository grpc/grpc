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

CROSS_RUBY=`mktemp tmpfile.XXXXXXXX`

curl https://raw.githubusercontent.com/rake-compiler/rake-compiler/v1.0.3/tasks/bin/cross-ruby.rake > $CROSS_RUBY

# See https://github.com/grpc/grpc/issues/12161 for verconf.h patch details
patch $CROSS_RUBY << EOF
--- cross-ruby.rake 2017-09-27 16:46:00.311020325 +0200
+++ patched 2017-09-27 16:49:46.127016895 +0200
@@ -133,7 +133,8 @@
     "--host=#{MINGW_HOST}",
     "--target=#{MINGW_TARGET}",
     "--build=#{RUBY_BUILD}",
-    '--enable-shared',
+    '--enable-static',
+    '--disable-shared',
     '--disable-install-doc',
     '--with-ext='
   ]
@@ -151,6 +152,7 @@
 # make
 file "#{USER_HOME}/builds/#{MINGW_HOST}/#{RUBY_CC_VERSION}/ruby.exe" => ["#{USER_HOME}/builds/#{MINGW_HOST}/#{RUBY_CC_VERSION}/Makefile"] do |t|
   chdir File.dirname(t.prerequisites.first) do
+    sh "test -s verconf.h || rm -f verconf.h"  # if verconf.h has size 0, make sure it gets re-built by make
     sh MAKE
   end
 end
EOF

MAKE="make -j8"

for v in 2.4.0 2.3.0 2.2.2 2.1.5 2.0.0-p645 ; do
  ccache -c
  rake -f $CROSS_RUBY cross-ruby VERSION=$v HOST=x86_64-darwin11
done

sed 's/x86_64-darwin-11/universal-darwin/' ~/.rake-compiler/config.yml > $CROSS_RUBY
mv $CROSS_RUBY ~/.rake-compiler/config.yml
