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
"""Generates the list of end2end test cases from generate_tests.bzl"""

import os
import sys
import yaml

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))
os.chdir(_ROOT)


def load(*args):
    """Replacement of bazel's load() function"""
    pass


def struct(**kwargs):
    return kwargs  # all the args as a dict


# generate_tests.bzl is now the source of truth for end2end tests.
# The .bzl file is basically a python file and we can "execute" it
# to get access to the variables it defines.
exec(
    compile(
        open('test/core/end2end/generate_tests.bzl', "rb").read(),
        'test/core/end2end/generate_tests.bzl', 'exec'))


def main():
    json = {
        # needed by end2end_tests.cc.template and end2end_nosec_tests.cc.template
        'core_end2end_tests':
            dict((t, END2END_TESTS[t]['secure']) for t in END2END_TESTS.keys())
    }
    print(yaml.dump(json))


if __name__ == '__main__':
    main()
