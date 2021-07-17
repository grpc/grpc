#!/usr/bin/env python3
#
# Copyright 2018 gRPC authors.
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

import argparse
import glob
import multiprocessing
import os
import shutil
import subprocess
import sys
from parse_link_map import parse_link_map

sys.path.append(
    os.path.join(os.path.dirname(sys.argv[0]), '..', '..', 'run_tests',
                 'python_utils'))
import check_on_pr

# Only show diff 1KB or greater
diff_threshold = 1000

size_labels = ('Core', 'ObjC', 'BoringSSL', 'Protobuf', 'Total')

argp = argparse.ArgumentParser(
    description='Binary size diff of gRPC Objective-C sample')

argp.add_argument('-d',
                  '--diff_base',
                  type=str,
                  help='Commit or branch to compare the current one to')

args = argp.parse_args()


def dir_size(dir):
    total = 0
    for dirpath, dirnames, filenames in os.walk(dir):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            total += os.stat(fp).st_size
    return total


def get_size(where, frameworks):
    build_dir = 'src/objective-c/examples/Sample/Build/Build-%s/' % where
    if not frameworks:
        link_map_filename = 'Build/Intermediates.noindex/Sample.build/Release-iphoneos/Sample.build/Sample-LinkMap-normal-arm64.txt'
        return parse_link_map(build_dir + link_map_filename)
    else:
        framework_dir = 'Build/Products/Release-iphoneos/Sample.app/Frameworks/'
        boringssl_size = dir_size(build_dir + framework_dir +
                                  'openssl.framework')
        core_size = dir_size(build_dir + framework_dir + 'grpc.framework')
        objc_size = dir_size(build_dir + framework_dir + 'GRPCClient.framework') + \
                    dir_size(build_dir + framework_dir + 'RxLibrary.framework') + \
                    dir_size(build_dir + framework_dir + 'ProtoRPC.framework')
        protobuf_size = dir_size(build_dir + framework_dir +
                                 'Protobuf.framework')
        app_size = dir_size(build_dir +
                            'Build/Products/Release-iphoneos/Sample.app')
        return core_size, objc_size, boringssl_size, protobuf_size, app_size


def build(where, frameworks):
    subprocess.check_call(['make', 'clean'])
    shutil.rmtree('src/objective-c/examples/Sample/Build/Build-%s' % where,
                  ignore_errors=True)
    subprocess.check_call(
        'CONFIG=opt EXAMPLE_PATH=src/objective-c/examples/Sample SCHEME=Sample FRAMEWORKS=%s ./build_one_example.sh'
        % ('YES' if frameworks else 'NO'),
        shell=True,
        cwd='src/objective-c/tests')
    os.rename('src/objective-c/examples/Sample/Build/Build',
              'src/objective-c/examples/Sample/Build/Build-%s' % where)


text = 'Objective-C binary sizes\n'
for frameworks in [False, True]:
    build('new', frameworks)
    new_size = get_size('new', frameworks)
    old_size = None

    if args.diff_base:
        old = 'old'
        where_am_i = subprocess.check_output(
            ['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip()
        subprocess.check_call(['git', 'checkout', '--', '.'])
        subprocess.check_call(['git', 'checkout', args.diff_base])
        subprocess.check_call(['git', 'submodule', 'update', '--force'])
        try:
            build('old', frameworks)
            old_size = get_size('old', frameworks)
        finally:
            subprocess.check_call(['git', 'checkout', '--', '.'])
            subprocess.check_call(['git', 'checkout', where_am_i])
            subprocess.check_call(['git', 'submodule', 'update', '--force'])

    text += ('***************FRAMEWORKS****************\n'
             if frameworks else '*****************STATIC******************\n')
    row_format = "{:>10}{:>15}{:>15}" + '\n'
    text += row_format.format('New size', '', 'Old size')
    if old_size == None:
        for i in range(0, len(size_labels)):
            text += ('\n' if i == len(size_labels) -
                     1 else '') + row_format.format('{:,}'.format(new_size[i]),
                                                    size_labels[i], '')
    else:
        has_diff = False
        for i in range(0, len(size_labels) - 1):
            if abs(new_size[i] - old_size[i]) < diff_threshold:
                continue
            if new_size[i] > old_size[i]:
                diff_sign = ' (>)'
            else:
                diff_sign = ' (<)'
            has_diff = True
            text += row_format.format('{:,}'.format(new_size[i]),
                                      size_labels[i] + diff_sign,
                                      '{:,}'.format(old_size[i]))
        i = len(size_labels) - 1
        if new_size[i] > old_size[i]:
            diff_sign = ' (>)'
        elif new_size[i] < old_size[i]:
            diff_sign = ' (<)'
        else:
            diff_sign = ' (=)'
        text += ('\n' if has_diff else '') + row_format.format(
            '{:,}'.format(new_size[i]), size_labels[i] + diff_sign,
            '{:,}'.format(old_size[i]))
        if not has_diff:
            text += '\n No significant differences in binary sizes\n'
    text += '\n'

print(text)

check_on_pr.check_on_pr('Binary Size', '```\n%s\n```' % text)
