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
import subprocess
import sys

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
argp.add_argument('--precommit', action='store_true')
argp.add_argument('--fix', action='store_true')
args = argp.parse_args()

# open the license text
with open('NOTICE.txt') as f:
    LICENSE_NOTICE = f.read().splitlines()

# license format by file extension
# key is the file extension, value is a format string
# that given a line of license text, returns what should
# be in the file
LICENSE_PREFIX_RE = {
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
    '.bazel': r'#\s*',
    '.bzl': r'#\s*',
    'Makefile': r'#\s*',
    'Dockerfile': r'#\s*',
    'BUILD': r'#\s*',
}

# The key is the file extension, while the value is a tuple of fields
# (header, prefix, footer).
# For example, for javascript multi-line comments, the header will be '/*', the
# prefix will be '*' and the footer will be '*/'.
# If header and footer are irrelevant for a specific file extension, they are
# set to None.
LICENSE_PREFIX_TEXT = {
    '.bat': (None, '@rem', None),
    '.c': (None, '//', None),
    '.cc': (None, '//', None),
    '.h': (None, '//', None),
    '.m': ('/**', ' *', ' */'),
    '.mm': ('/**', ' *', ' */'),
    '.php': ('/**', ' *', ' */'),
    '.js': ('/**', ' *', ' */'),
    '.py': (None, '#', None),
    '.pyx': (None, '#', None),
    '.pxd': (None, '#', None),
    '.pxi': (None, '#', None),
    '.rb': (None, '#', None),
    '.sh': (None, '#', None),
    '.proto': (None, '//', None),
    '.cs': (None, '//', None),
    '.mak': (None, '#', None),
    '.bazel': (None, '#', None),
    '.bzl': (None, '#', None),
    'Makefile': (None, '#', None),
    'Dockerfile': (None, '#', None),
    'BUILD': (None, '#', None),
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

_ENFORCE_CPP_STYLE_COMMENT_PATH_PREFIX = tuple([
    'include/grpc++/',
    'include/grpcpp/',
    'src/core/',
    'src/cpp/',
    'test/core/',
    'test/cpp/',
    'fuzztest/',
])

RE_YEAR = r'Copyright (?P<first_year>[0-9]+\-)?(?P<last_year>[0-9]+) ([Tt]he )?gRPC [Aa]uthors(\.|)'
RE_LICENSE = dict(
    (k, r'\n'.join(LICENSE_PREFIX_RE[k] +
                   (RE_YEAR if re.search(RE_YEAR, line) else re.escape(line))
                   for line in LICENSE_NOTICE))
    for k, v in list(LICENSE_PREFIX_RE.items()))

RE_C_STYLE_COMMENT_START = r'^/\*\s*\n'
RE_C_STYLE_COMMENT_OPTIONAL_LINE = r'(?:\s*\*\s*\n)*'
RE_C_STYLE_COMMENT_END = r'\s*\*/'
RE_C_STYLE_COMMENT_LICENSE = RE_C_STYLE_COMMENT_START + RE_C_STYLE_COMMENT_OPTIONAL_LINE + r'\n'.join(
    r'\s*(?:\*)\s*' + (RE_YEAR if re.search(RE_YEAR, line) else re.escape(line))
    for line in LICENSE_NOTICE
) + r'\n' + RE_C_STYLE_COMMENT_OPTIONAL_LINE + RE_C_STYLE_COMMENT_END
RE_CPP_STYLE_COMMENT_LICENSE = r'\n'.join(
    r'\s*(?://)\s*' + (RE_YEAR if re.search(RE_YEAR, line) else re.escape(line))
    for line in LICENSE_NOTICE)

YEAR = datetime.datetime.now().year

LICENSE_YEAR = f'Copyright {YEAR} gRPC authors.'


def join_license_text(header, prefix, footer, notice):
    text = (header + '\n') if header else ""

    def add_prefix(prefix, line):
        # Don't put whitespace between prefix and empty line to avoid having
        # trailing whitespaces.
        return prefix + ('' if len(line) == 0 else ' ') + line

    text += '\n'.join(
        add_prefix(prefix, (LICENSE_YEAR if re.search(RE_YEAR, line) else line))
        for line in LICENSE_NOTICE)
    text += '\n'
    if footer:
        text += footer + '\n'
    return text


LICENSE_TEXT = dict(
    (k,
     join_license_text(LICENSE_PREFIX_TEXT[k][0], LICENSE_PREFIX_TEXT[k][1],
                       LICENSE_PREFIX_TEXT[k][2], LICENSE_NOTICE))
    for k, v in list(LICENSE_PREFIX_TEXT.items()))

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
        print(('%s: %s' % (why, filename)))
    else:
        print(filename)


def write_copyright(license_text, file_text, filename):
    shebang = ""
    lines = file_text.split("\n")
    if lines and lines[0].startswith("#!"):
        shebang = lines[0] + "\n"
        file_text = file_text[len(shebang):]

    rewritten_text = shebang + license_text + "\n" + file_text
    with open(filename, 'w') as f:
        f.write(rewritten_text)


def replace_copyright(license_text, file_text, filename):
    m = re.search(RE_C_STYLE_COMMENT_LICENSE, text)
    if m:
        rewritten_text = license_text + file_text[m.end():]
        with open(filename, 'w') as f:
            f.write(rewritten_text)
        return True
    return False


# scan files, validate the text
ok = True
filename_list = []
try:
    filename_list = subprocess.check_output(FILE_LIST_COMMAND,
                                            shell=True).decode().splitlines()
except subprocess.CalledProcessError:
    sys.exit(0)

for filename in filename_list:
    enforce_cpp_style_comment = False
    if filename in _EXEMPT:
        continue
    # Skip check for upb generated code.
    if (filename.endswith('.upb.h') or filename.endswith('.upb.c') or
            filename.endswith('.upbdefs.h') or filename.endswith('.upbdefs.c')):
        continue
    ext = os.path.splitext(filename)[1]
    base = os.path.basename(filename)
    if filename.startswith(_ENFORCE_CPP_STYLE_COMMENT_PATH_PREFIX) and ext in [
            '.cc', '.h'
    ]:
        enforce_cpp_style_comment = True
        re_license = RE_CPP_STYLE_COMMENT_LICENSE
        license_text = LICENSE_TEXT[ext]
    elif ext in RE_LICENSE:
        re_license = RE_LICENSE[ext]
        license_text = LICENSE_TEXT[ext]
    elif base in RE_LICENSE:
        re_license = RE_LICENSE[base]
        license_text = LICENSE_TEXT[base]
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
    elif enforce_cpp_style_comment:
        log(1, 'copyright missing or does not use cpp-style copyright header',
            filename)
        if args.fix:
            # Attempt fix: search for c-style copyright header and replace it
            # with cpp-style copyright header. If that doesn't work
            # (e.g. missing copyright header), write cpp-style copyright header.
            if not replace_copyright(license_text, text, filename):
                write_copyright(license_text, text, filename)
        ok = False
    elif 'DO NOT EDIT' not in text:
        if args.fix:
            write_copyright(license_text, text, filename)
            log(1, 'copyright missing (fixed)', filename)
        else:
            log(1, 'copyright missing', filename)
        ok = False

if not ok and not args.fix:
    print(
        'You may use following command to automatically fix copyright headers:')
    print('    tools/distrib/check_copyright.py --fix')

sys.exit(0 if ok else 1)
