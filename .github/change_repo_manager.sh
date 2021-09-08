#!/bin/bash
#
# Copyright 2020 The gRPC authors.
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

set -e

if [ $# -lt 1 ];then
  echo "Usage: $0 github-id"
  exit 1
fi

echo "Change repo manager to $1"

BASE_PATH=$(dirname $0)

for file in bug_report.md cleanup_request.md feature_request.md question.md
do
	sed -i".bak" -E "s/assignees: ([a-zA-Z0-9-]+)/assignees: $1/" "$BASE_PATH/ISSUE_TEMPLATE/$file"
  rm "$BASE_PATH/ISSUE_TEMPLATE/$file.bak"
done

sed -i".bak" -E "s/^@([a-zA-Z0-9-]+)/@$1/" "$BASE_PATH/pull_request_template.md"
rm "$BASE_PATH/pull_request_template.md.bak"

echo "Done"
