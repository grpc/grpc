#!/usr/bin/env python
# Copyright 2017 gRPC authors.
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
#
# This script is invoked by a Jenkins pull request job and executes all
# args passed to this script if the pull request affect C/C++ code

# Populates a DNS managed zone on DNS with records for testing

import argparse
import subprocess

import dns_records_config


argp = argparse.ArgumentParser(description='')
argp.add_argument('--dry_run', default=False, action='store_const', const=True,
                  help='Print the commands that would be ran, without running them')
args = argp.parse_args()


def main():
  cmds = []
  cmd = ('gcloud alpha dns managed-zones create '
         '%s '
         '--dns-name=%s '
         '--description="GCE-DNS-private-zone-for-GRPC-testing" '
         '--visibility=private '
         '--networks=default') % (dns_records_config.ZONE_NAME,
                                  dns_records_config.ZONE_DNS)

  if args.dry_run:
    print(cmd)
  else:
    subprocess.call(cmd.split(' '))

main()
