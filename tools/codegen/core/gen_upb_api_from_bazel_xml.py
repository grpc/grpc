#!/usr/bin/env python3

# Copyright 2021 gRPC authors.
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

# This script generates upb source files (e.g. *.upb.c) from all upb targets
# in Bazel BUILD file. These generate upb files are for non-Bazel build such
# as makefile and python build which cannot generate them at the build time.
#
# As an example, for the following upb target
#
#   grpc_upb_proto_library(
#     name = "grpc_health_upb",
#     deps = ["//src/proto/grpc/health/v1:health_proto_descriptor"],
#   )
#
# this will generate these upb source files at src/core/ext/upb-generated.
#
#   src/proto/grpc/health/v1/health.upb.c
#   src/proto/grpc/health/v1/health.upb.h

import argparse
import collections
import os
import shutil
import subprocess
import xml.etree.ElementTree

# Rule object representing the UPB rule of Bazel BUILD.
Rule = collections.namedtuple('Rule', 'name type srcs deps proto_files')

BAZEL_BIN = 'tools/bazel'


def parse_bazel_rule(elem):
    '''Returns a rule from bazel XML rule.'''
    srcs = []
    deps = []
    for child in elem:
        if child.tag == 'list' and child.attrib['name'] == 'srcs':
            for tag in child:
                if tag.tag == 'label':
                    srcs.append(tag.attrib['value'])
        if child.tag == 'list' and child.attrib['name'] == 'deps':
            for tag in child:
                if tag.tag == 'label':
                    deps.append(tag.attrib['value'])
    return Rule(elem.attrib['name'], elem.attrib['class'], srcs, deps, [])


def read_upb_bazel_rules():
    '''Runs bazel query on given package file and returns all upb rules.'''
    # Use a wrapper version of bazel in gRPC not to use system-wide bazel
    # to avoid bazel conflict when running on Kokoro.
    result = subprocess.check_output(
        [BAZEL_BIN, 'query', '--output', 'xml', '--noimplicit_deps', '//:all'])
    root = xml.etree.ElementTree.fromstring(result)
    rules = [
        parse_bazel_rule(elem)
        for elem in root
        if elem.tag == 'rule' and elem.attrib['class'] in [
            'upb_proto_library',
            'upb_proto_reflection_library',
        ]
    ]
    # query all dependencies of upb rules to get a list of proto files
    all_deps = [dep for rule in rules for dep in rule.deps]
    result = subprocess.check_output([
        BAZEL_BIN, 'query', '--output', 'xml', '--noimplicit_deps',
        ' union '.join(all_deps)
    ])
    root = xml.etree.ElementTree.fromstring(result)
    dep_rules = {}
    for dep_rule in (
            parse_bazel_rule(elem) for elem in root if elem.tag == 'rule'):
        dep_rules[dep_rule.name] = dep_rule
    # add proto files to upb rules
    for rule in rules:
        if len(rule.deps) == 1:
            dep_rule = dep_rules.get(rule.deps[0], None)
            if dep_rule:
                rule.proto_files.extend(dep_rule.srcs)
    return rules


def build_upb_bazel_rules(rules):
    result = subprocess.check_output([BAZEL_BIN, 'build'] +
                                     [rule.name for rule in rules])


def get_upb_path(proto_path, ext):
    return proto_path.replace(':', '/').replace('.proto', ext)


def get_bazel_bin_root_path(elink):
    BAZEL_BIN_ROOT = 'bazel-bin/'
    if elink[0].startswith('@'):
        # external
        return os.path.join(BAZEL_BIN_ROOT, 'external',
                            elink[0].replace('@', '').replace('//', ''))
    else:
        # internal
        return BAZEL_BIN_ROOT


def copy_upb_generated_files(rules, args):
    EXTERNAL_LINKS = [
        ('@com_google_protobuf//', ':src/'),
    ]
    for rule in rules:
        files = []
        elink = ('//', '')
        for external_link in EXTERNAL_LINKS:
            if rule.proto_files[0].startswith(external_link[0]):
                elink = external_link
                break
        if rule.type == 'upb_proto_library':
            for proto_file in rule.proto_files:
                proto_file = proto_file[len(elink[0]) + len(elink[1]):]
                files.append(get_upb_path(proto_file, '.upb.h'))
                files.append(get_upb_path(proto_file, '.upb.c'))
            output_dir = args.upb_out
        else:
            for proto_file in rule.proto_files:
                proto_file = proto_file[len(elink[0]) + len(elink[1]):]
                files.append(get_upb_path(proto_file, '.upbdefs.h'))
                files.append(get_upb_path(proto_file, '.upbdefs.c'))
            output_dir = args.upbdefs_out
        for file in files:
            src = os.path.join(get_bazel_bin_root_path(elink), file)
            dst = os.path.join(output_dir, file)
            if args.verbose:
                print('Copy:')
                print('    {0}'.format(src))
                print(' -> {0}'.format(dst))
            os.makedirs(os.path.split(dst)[0], exist_ok=True)
            shutil.copyfile(src, dst)


parser = argparse.ArgumentParser(description='UPB code-gen from bazel')
parser.add_argument('--verbose', default=False, action='store_true')
parser.add_argument('--upb_out',
                    default='src/core/ext/upb-generated',
                    help='Output directory for upb targets')
parser.add_argument('--upbdefs_out',
                    default='src/core/ext/upbdefs-generated',
                    help='Output directory for upbdefs targets')


def main():
    args = parser.parse_args()
    rules = read_upb_bazel_rules()
    if args.verbose:
        print('Rules:')
        for rule in rules:
            print('  name={0} type={1} proto_files={2}'.format(
                rule.name, rule.type, rule.proto_files))
    if rules:
        build_upb_bazel_rules(rules)
        copy_upb_generated_files(rules, args)


if __name__ == '__main__':
    main()
