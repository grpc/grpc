#!/bin/bash
# Copyright 2016 gRPC authors.
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

# change to root directory
cd $(dirname $0)/../..

function find_without_newline() {
  find . -type f -not -path './third_party/*' -and \(  \
                             -name '*.c'               \
                         -or -name '*.cc'              \
                         -or -name '*.proto'           \
                         -or -name '*.rb'              \
                         -or -name '*.py'              \
                         -or -name '*.cs'              \
                         -or -name '*.sh' \) -print0   \
                         | while IFS= read -r -d '' f; do
    if [[ ! -z $f ]]; then
      if [[ $(tail -c 1 "$f") != $NEWLINE ]]; then
        echo "Error: file '$f' is missing a trailing newline character."
        if $2; then  # fix
          sed -i -e '$a\' $f
          echo 'Fixed!'
        fi
      fi
    fi
  done
}

if [[ $# == 1 && $1 == '--fix' ]]; then
  ERRORS=$(find_without_newline true)
else
  ERRORS=$(find_without_newline false)
fi

if [[ "$ERRORS" != '' ]]; then
  echo "$ERRORS"
  if ! $FIX; then
    exit 1
  fi
fi
