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

"""Prints out TXT data for a grpc service config in a form that is
   suitable for copy-paste into a BIND zone file."""

import os
import json
import argparse

_GCLOUD = 'gcloud'
_TWISTED = 'twisted'
_BIND9 = 'bind9'

def convert_service_config_to_txt_data(service_config_json_path,
                                       zone_file_type):
  with open(service_config_json_path) as service_config_json_in:
    r_data = json.dumps(json.load(service_config_json_in))
    r_data = r_data.replace(' ', '').replace('\n', '')
    r_data = 'grpc_config=%s' % r_data
    chunks = []
    cur = 0
    while len(r_data[cur:]) > 0:
      next_chunk = ''
      while len(next_chunk) < 255 and len(r_data[cur:]) > 0:
        if r_data[cur] == '"' and zone_file_type in [_GCLOUD, _BIND9]:
          # Unlike gcloud and bind9 zone file parsers, twisted
          # BindAuthority requires that inner quotation marks are
          # not escaped.
          to_add = '\\"'
          if len(next_chunk) + len(to_add) > 255:
            chunks.append(next_chunk)
            next_chunk = ''
          next_chunk += to_add
        else:
          next_chunk += r_data[cur]
        cur += 1
      chunks.append(next_chunk)
    out = '(\n'
    for c in chunks:
      if len(c) > 255:
        raise Exception(('Bug: TXT string is > 255 character length limit. '
                         'Length: %s' % len(c)))
      if zone_file_type == _GCLOUD:
        # Observably, gcloud dns zone file parser also requires that
        # "inner" backslashes and "inner" quotation marks themselves
        # be escaped, and it also requires that each "TXT string" be
        # wrapped in quotation marks. But the "outer quotation marks"
        # and "outer backslashes" are not counted towards the
        # single-string 255 character limit.
        c = c.replace('\\"', '\\\\\\"')
      if zone_file_type in [_GCLOUD, _BIND9]:
        c = '\"%s\"' % c
      out += '    %s\n' % c
    out += ')'
    return out

def main():
  argp = argparse.ArgumentParser(
      description='Local DNS Server for resolver tests')
  argp.add_argument('-s', '--service_config_json_path',
                    default=None,
                    type=str,
                    help=('Path to a JSON file containing a grpc '
                          'service config.'))
  argp.add_argument('-z', '--zone_file_type',
                  choices=[_GCLOUD, _BIND9, _TWISTED],
                  nargs='+',
                  default=[_TWISTED],
                  help=('Create the TXT record data in a format that '
                        'is suitable for a particular server '
                        'or zone-file-parser'))
  args = argp.parse_args()
  out = convert_service_config_to_txt_data(args.service_config_json_path,
                                           args.format[0])
  print(out)

if __name__ == '__main__':
  main()
