#!/bin/bash

# Copyright 2020 gRPC authors.
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

set -euo pipefail

if ! command -v gawk > /dev/null; then
    >&2 echo "ERROR: 'gawk' not installed"
    exit 1
fi

cd $(dirname $0)

COMMAND="${1:-}"

# parse class and methods
for FILENAME in call_credentials.c call.c channel.c channel_credentials.c \
                server_credentials.c server.c timeval.c ; do
    CLASS_NAME=$(sed -r 's/(^|_)(\w)/\U\2/g' <<< "${FILENAME%.*}")
    if [[ "$COMMAND" == "generate" ]]; then
        echo Generating lib/Grpc/$CLASS_NAME.php ...
        gawk -f php_extension_doxygen_filter.awk ../ext/grpc/$FILENAME \
            > ../lib/Grpc/$CLASS_NAME.php
    elif [[ "$COMMAND" == "cleanup" ]]; then
        rm ../lib/Grpc/$CLASS_NAME.php
    else
        >&2 echo "Missing or wrong command. Usage: '$(basename $0) <generate|cleanup>'"
        exit 1
    fi
done

# parse constants
if [[ "$COMMAND" == "generate" ]]; then
    echo Generating lib/Grpc/Constants.php ...
    gawk -f php_extension_doxygen_filter.awk ../ext/grpc/php_grpc.c \
        > ../lib/Grpc/Constants.php
elif [[ "$COMMAND" == "cleanup" ]]; then
    rm ../lib/Grpc/Constants.php
fi
