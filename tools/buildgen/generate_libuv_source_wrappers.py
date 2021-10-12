#!/usr/bin/env python3
# Copyright 2020 The gRPC Authors
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
"""Creates wrapper source and header files for all libuv code.

Parts of the gRPC build require a flat list of files to compile (see
src/libuv/gen_build_yaml.py). Libuv's source code does not lend itself to that
kind of build configuration, so this script creates wrapper code for every libuv
source file that conditionally compiles the original source file if it supports
the current platform.
"""
import os

from mako.runtime import Context
from mako.template import Template

PROJECT_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
# load Mako templates
TEMPLATES = {}
tmpl_path = os.path.join(PROJECT_ROOT, 'src', 'libuv', 'templates')
for tmpl_fn in os.listdir(tmpl_path):
    TEMPLATES[tmpl_fn] = Template(filename=os.path.join(tmpl_path, tmpl_fn))


# reuse configuration from the bazel build
# Dummy functions for the bazel import
def config_setting(*args, **kwargs):
    pass


def cc_library(*args, **kwargs):
    pass


def select(*args, **kwargs):
    return []


def render_template(template: Template, src_filename: str) -> None:
    print('processing:', src_filename)
    out_filename = os.path.join(PROJECT_ROOT, 'src', 'libuv', src_filename)
    if not os.path.isdir(os.path.dirname(out_filename)):
        os.makedirs(os.path.dirname(out_filename))
    with open(out_filename, 'w') as outfile:
        include_filename = os.path.join('third_party', 'libuv', src_filename)
        template.render_context(Context(outfile, filename=include_filename))


exec(open(os.path.join(PROJECT_ROOT, 'third_party', 'libuv.BUILD')).read())
for src_filename in COMMON_LIBUV_SOURCES:
    render_template(TEMPLATES['common.cc.template'], src_filename)
for src_filename in COMMON_LIBUV_HEADERS:
    render_template(TEMPLATES['common.h.template'], src_filename)
for src_filename in UNIX_LIBUV_SOURCES:
    render_template(TEMPLATES['unix.cc.template'], src_filename)
for src_filename in UNIX_LIBUV_HEADERS:
    render_template(TEMPLATES['unix.h.template'], src_filename)
for src_filename in LINUX_LIBUV_SOURCES:
    render_template(TEMPLATES['linux.cc.template'], src_filename)
for src_filename in LINUX_LIBUV_HEADERS:
    render_template(TEMPLATES['linux.h.template'], src_filename)
for src_filename in WINDOWS_LIBUV_SOURCES:
    render_template(TEMPLATES['windows.cc.template'], src_filename)
for src_filename in WINDOWS_LIBUV_HEADERS:
    render_template(TEMPLATES['windows.h.template'], src_filename)
for src_filename in ANDROID_LIBUV_SOURCES:
    render_template(TEMPLATES['android.cc.template'], src_filename)
for src_filename in ANDROID_LIBUV_HEADERS:
    render_template(TEMPLATES['android.h.template'], src_filename)
for src_filename in DARWIN_LIBUV_SOURCES:
    render_template(TEMPLATES['darwin.cc.template'], src_filename)
for src_filename in DARWIN_LIBUV_HEADERS:
    render_template(TEMPLATES['darwin.h.template'], src_filename)
