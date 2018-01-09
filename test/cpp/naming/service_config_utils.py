#!/usr/bin/env python2.7
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

"""Utility for packing grpc service config JSON into TXT DNS records"""

import json

def txt_data_list_from_service_config_json(record_name, gcloud_form):
  service_config_json_path = 'test/cpp/naming/service_configs/%s.json' % record_name
  with open(service_config_json_path) as service_config_json_in:
    all_data = json.dumps(json.load(service_config_json_in))
    all_data = all_data.replace(' ', '').replace('\n', '')
    all_data = 'grpc_config=%s' % all_data
    chunks = []
    cur = 0
    # Split TXT records that span more than 255 characters (the single
    # string length-limit in DNS) into multiple strings.
    # The quoting and escaping rules depend on what the TXT record is
    # intented for, "gcloud upload" or "twisted DNS server".
    # If "gcloud upload" is the target, then each string
    # needs to be wrapped with double-quotes, and all inner double-quotes
    # need to be escaped. The wrapping double-quotes and inner backslashes can be
    # counted towards the 255 character length limit
    # though (as observed with gcloud), so make sure all strings
    # fit within that limit.
    while len(all_data[cur:]) > 0:
      next_chunk = ""
      if gcloud_form:
        next_chunk += '\"'
      while len(next_chunk) < 254 and len(all_data[cur:]) > 0:
        if all_data[cur] == '\"':
          if len(next_chunk) < 253:
            if gcloud_form:
              next_chunk += '\\'
            next_chunk += all_data[cur]
          else:
            break
        else:
          next_chunk += all_data[cur]
        cur += 1
      if gcloud_form:
        next_chunk += '\"'
      if len(next_chunk) > 255:
        raise Exception('Bug: next chunk is too long.')
      chunks.append(next_chunk)
    return chunks
