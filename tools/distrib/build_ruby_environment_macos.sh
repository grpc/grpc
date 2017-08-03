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

patch $CROSS_RUBY << EOF
--- cross-ruby.rake	2016-02-05 16:26:53.000000000 -0800
+++ cross-ruby.rake.patched	2016-02-05 16:27:33.000000000 -0800
@@ -133,7 +133,8 @@
     "--host=#{MINGW_HOST}",
     "--target=#{MINGW_TARGET}",
     "--build=#{RUBY_BUILD}",
-    '--enable-shared',
+    '--enable-static',
+    '--disable-shared',
     '--disable-install-doc',
     '--without-tk',
     '--without-tcl'
EOF

MAKE="make -j8"

for v in 2.4.0 2.3.0 2.2.2 2.1.5 2.0.0-p645 ; do
  ccache -c
  rake -f $CROSS_RUBY cross-ruby VERSION=$v HOST=x86_64-darwin11
done

sed 's/x86_64-darwin-11/universal-darwin/' ~/.rake-compiler/config.yml > $CROSS_RUBY
mv $CROSS_RUBY ~/.rake-compiler/config.yml
