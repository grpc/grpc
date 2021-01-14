#!/usr/bin/env python3

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

import argparse
import datetime
import os
import re
import sys
import subprocess

# find our home
ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

# parse command line
argp = argparse.ArgumentParser(description='copyright checker')
argp.add_argument('-o',
                  '--output',
                  default='details',
                  choices=['list', 'details'])
argp.add_argument('-s', '--skips', default=0, action='store_const', const=1)
argp.add_argument('-a', '--ancient', default=0, action='store_const', const=1)
argp.add_argument('--precommit', default=False, action='store_true')
args = argp.parse_args()

# open the license text
with open('NOTICE.txt') as f:
    LICENSE_NOTICE = f.read().splitlines()

# license format by file extension
# key is the file extension, value is a format string
# that given a line of license text, returns what should
# be in the file
LICENSE_PREFIX = {
    '.bat': r'@rem\s*',
    '.c': r'\s*(?://|\*)\s*',
    '.cc': r'\s*(?://|\*)\s*',
    '.h': r'\s*(?://|\*)\s*',
    '.m': r'\s*\*\s*',
    '.mm': r'\s*\*\s*',
    '.php': r'\s*\*\s*',
    '.js': r'\s*\*\s*',
    '.py': r'#\s*',
    '.pyx': r'#\s*',
    '.pxd': r'#\s*',
    '.pxi': r'#\s*',
    '.rb': r'#\s*',
    '.sh': r'#\s*',
    '.proto': r'//\s*',
    '.cs': r'//\s*',
    '.mak': r'#\s*',
    'Makefile': r'#\s*',
    'Dockerfile': r'#\s*',
    'BUILD': r'#\s*',
}

_EXEMPT = frozenset((
    # Generated protocol compiler output.
    'examples/python/helloworld/helloworld_pb2.py',
    'examples/python/helloworld/helloworld_pb2_grpc.py',
    'examples/python/multiplex/helloworld_pb2.py',
    'examples/python/multiplex/helloworld_pb2_grpc.py',
    'examples/python/multiplex/route_guide_pb2.py',
    'examples/python/multiplex/route_guide_pb2_grpc.py',
    'examples/python/route_guide/route_guide_pb2.py',
    'examples/python/route_guide/route_guide_pb2_grpc.py',

    # Generated doxygen config file
    'tools/doxygen/Doxyfile.php',

    # An older file originally from outside gRPC.
    'src/php/tests/bootstrap.php',
    # census.proto copied from github
    'tools/grpcz/census.proto',
    # status.proto copied from googleapis
    'src/proto/grpc/status/status.proto',

    # Gradle wrappers used to build for Android
    'examples/android/helloworld/gradlew.bat',
    'src/android/test/interop/gradlew.bat',

    # Designer-generated source
    'examples/csharp/HelloworldXamarin/Droid/Resources/Resource.designer.cs',
    'examples/csharp/HelloworldXamarin/iOS/ViewController.designer.cs',

    # BoringSSL generated header. It has commit version information at the head
    # of the file so we cannot check the license info.
    'src/boringssl/boringssl_prefix_symbols.h',
))

RE_YEAR = r'Copyright (?P<first_year>[0-9]+\-)?(?P<last_year>[0-9]+) ([Tt]he )?gRPC [Aa]uthors(\.|)'
RE_LICENSE = dict(
    (k, r'\n'.join(LICENSE_PREFIX[k] +
                   (RE_YEAR if re.search(RE_YEAR, line) else re.escape(line))
                   for line in LICENSE_NOTICE))
    for k, v in LICENSE_PREFIX.items())

if args.precommit:
    FILE_LIST_COMMAND = 'git status -z | grep -Poz \'(?<=^[MARC][MARCD ] )[^\s]+\''
else:
    FILE_LIST_COMMAND = 'git ls-tree -r --name-only -r HEAD | ' \
                        'grep -v ^third_party/ |' \
                        'grep -v "\(ares_config.h\|ares_build.h\)"'


def load(name):
    with open(name) as f:
        return f.read()


def save(name, text):
    with open(name, 'w') as f:
        f.write(text)


assert (re.search(RE_LICENSE['Makefile'], load('Makefile')))


def log(cond, why, filename):
    if not cond:
        return
    if args.output == 'details':
        print('%s: %s' % (why, filename))
    else:
        print(filename)


# scan files, validate the text
ok = True
filename_list = []
try:
    filename_list = subprocess.check_output(FILE_LIST_COMMAND,
                                            shell=True).decode().splitlines()
except subprocess.CalledProcessError:
    sys.exit(0)

for filename in filename_list:
    if filename in _EXEMPT:
        continue
    # Skip check for upb generated code.
    if (filename.endswith('.upb.h') or filename.endswith('.upb.c') or
            filename.endswith('.upbdefs.h') or filename.endswith('.upbdefs.c')):
        continue
    ext = os.path.splitext(filename)[1]
    base = os.path.basename(filename)
    if ext in RE_LICENSE:
        re_license = RE_LICENSE[ext]
    elif base in RE_LICENSE:
        re_license = RE_LICENSE[base]
    else:
        log(args.skips, 'skip', filename)
        continue
    try:
        text = load(filename)
    except:
        continue
    m = re.search(re_license, text)
    if m:
        pass
    elif 'DO NOT EDIT' not in text:
        log(1, 'copyright missing', filename)
        ok = False

sys.exit(0 if ok else 1)
