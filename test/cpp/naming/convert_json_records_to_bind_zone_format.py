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

"""Converts a DNS record set in custom JSON format to BIND zone file format."""

import json
import sys
import argparse

argp = argparse.ArgumentParser(description=('Converter between DNS record sets in a custom '
                                            'JSON format and BIND zone file format.'))
argp.add_argument('-f', '--json_file', default=None, type=str,
                  help='Path to file containing DNS records in a custom JSON format.')
argp.add_argument('-g', '--python_dns_server_format',
                  default=False,
                  action='store_const',
                  const=True,
                  help=('Create the zone file for upload to a GCE private zone. '
                        'This influences format of TXT data.'))
args = argp.parse_args()
_NAME_COL_WIDTH = 128
_TTL_COL_WIDTH = 8
_IN_COL_WIDTH = 4
_TYPE_COL_WIDTH = 8

def _data_for_type(r_data, r_type):
  if r_type in ['SRV', 'A', 'AAAA', 'SOA']:
    return r_data
  assert r_type == 'TXT', 'bad record type %s' % r_type
  chunks = []
  cur = 0
  while len(r_data[cur:]) > 0:
    next_chunk = ''
    while len(next_chunk) < 255 and len(r_data[cur:]) > 0:
      if r_data[cur] == '\"' and not args.python_dns_server_format:
        # Observably, gcloud requires quotation marks within TXT strings to be
        # escaped, and the backslashes used for escaping are added towards the
        # single-string 255 character limit. However, twisted BindAuthority
        # requires that inner quotation marks are not escaped.
        to_add = '\\\"'
        while len(to_add) > 0:
          if len(next_chunk) > 255:
            chunks.append(next_chunk)
            next_chunk = ''
          next_chunk += to_add[0]
          to_add = to_add[1:]
      else:
        next_chunk += r_data[cur]
      cur += 1
    chunks.append(next_chunk)
  out = '(\n'
  for c in chunks:
    if len(c) > 255:
      raise Exception('Bug: TXT string is > 255 character length limit. Length: %s' % len(c))
    if not args.python_dns_server_format:
      # Observably, gcloud dns zone file parser also requires that
      # "inner" backslashes and "inner" quotation marks themselves be escaped,
      # and it also requires that each "TXT string" be wrapped in quotation
      # marks. But the "outer quotation marks" and "outer backslashes" are not counted
      # towards the single-string 255 character limit.
      c = '\"%s\"' % c.replace('\\\"', '\\\\\\\"')
    out += '    %s\n' % c
  out += ')'
  return out

def _fill_column(data, col_width):
  return data + ' ' * (col_width - len(data))

def main():
  with open(args.json_file, 'r') as json_file:
    json_data = json.load(json_file)
    common_zone_name = json_data['common_zone_name']
    # For the python DNS server's zone file, we need to avoid use of the $ORIGIN
    # keyword and instead use FQDN in all names in the zone file, because older
    # versions of twisted have a bug in which use of $ORIGIN adds an an extra
    # period at the end of names in it's domain name lookup table. (see
    # https://github.com/twisted/twisted/pull/579 which fixes the issue).
    # TODO: use the $ORIGIN keyword once this doesn't have to be compatible with
    # older versions of twisted.
    if not args.python_dns_server_format:
      sys.stdout.write('$ORIGIN %s\n' % common_zone_name)
    for r in json_data['records']:
      all_data = [r['data']]
      if r['type'] in ['AAAA', 'A', 'SRV']:
        all_data = r['data'].split(',')
      for single_data in all_data:
        r_name = r['name']
        if args.python_dns_server_format:
          r_name += '.%s' % common_zone_name
        line = ''
        line += _fill_column(r_name, _NAME_COL_WIDTH)
        line += _fill_column(r['ttl'], _TTL_COL_WIDTH)
        line += _fill_column('IN', _IN_COL_WIDTH)
        line += _fill_column(r['type'], _TYPE_COL_WIDTH)
        line += _data_for_type(single_data, r['type'])
        line += '\n'
        sys.stdout.write(line)

if __name__ == '__main__':
  main()
