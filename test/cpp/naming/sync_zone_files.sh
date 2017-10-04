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

# This file is auto-generated

set -ex

cd $(dirname $0)

python convert_json_records_to_bind_zone_format.py --json_file local_dns_server_records.json --python_dns_server_format > local_dns_server_records.zone
python convert_json_records_to_bind_zone_format.py --json_file private_dns_zone_records.json > private_dns_zone_records.zone
