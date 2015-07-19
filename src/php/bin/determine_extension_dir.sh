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
set -e
default_extension_dir=$(php-config --extension-dir)
if command -v brew > /dev/null && \
   brew ls --versions | grep php5[56]-grpc > /dev/null; then
  # the grpc php extension was installed by homebrew
  :
elif [ ! -e $default_extension_dir/grpc.so ]; then
  # the grpc extension is not found in the default PHP extension dir
  # try the source modules directory
  module_dir=../ext/grpc/modules
  if [ ! -e $module_dir/grpc.so ]; then
    echo "Please run 'phpize && ./configure && make' from ext/grpc first"
    exit 1
  fi
  # sym-link in system supplied extensions
  for f in $default_extension_dir/*.so; do
    ln -s $f $module_dir/$(basename $f) &> /dev/null || true
  done
  extension_dir="-d extension_dir=${module_dir} -d extension=grpc.so"
else
  extension_dir="-d extension=grpc.so"
fi
