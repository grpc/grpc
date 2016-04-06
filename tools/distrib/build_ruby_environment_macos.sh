#!/bin/bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -ex

rm -rf ~/.rake-compiler

CROSS_RUBY=`mktemp tmpfile.XXXXXXXX`

curl https://raw.githubusercontent.com/rake-compiler/rake-compiler/v0.9.5/tasks/bin/cross-ruby.rake > $CROSS_RUBY

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

for v in 2.3.0 2.2.2 2.1.5 2.0.0-p645 ; do
  rake -f $CROSS_RUBY cross-ruby VERSION=$v HOST=x86_64-darwin11
done

sed 's/x86_64-darwin-11/universal-darwin/' ~/.rake-compiler/config.yml > $CROSS_RUBY
mv $CROSS_RUBY ~/.rake-compiler/config.yml
