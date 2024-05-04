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

# Eliminate the kind of redundant namespace qualifiers that tend to
# creep in when converting C to C++.

import collections
import os
import re
import sys


def find_closing_mustache(contents, initial_depth):
    """Find the closing mustache for a given number of open mustaches."""
    depth = initial_depth
    start_len = len(contents)
    while contents:
        # Skip over strings.
        if contents[0] == '"':
            contents = contents[1:]
            while contents[0] != '"':
                if contents.startswith("\\\\"):
                    contents = contents[2:]
                elif contents.startswith('\\"'):
                    contents = contents[2:]
                else:
                    contents = contents[1:]
            contents = contents[1:]
        # And characters that might confuse us.
        elif (
            contents.startswith("'{'")
            or contents.startswith("'\"'")
            or contents.startswith("'}'")
        ):
            contents = contents[3:]
        # Skip over comments.
        elif contents.startswith("//"):
            contents = contents[contents.find("\n") :]
        elif contents.startswith("/*"):
            contents = contents[contents.find("*/") + 2 :]
        # Count up or down if we see a mustache.
        elif contents[0] == "{":
            contents = contents[1:]
            depth += 1
        elif contents[0] == "}":
            contents = contents[1:]
            depth -= 1
            if depth == 0:
                return start_len - len(contents)
        # Skip over everything else.
        else:
            contents = contents[1:]
    return None


def is_a_define_statement(match, body):
    """See if the matching line begins with #define"""
    # This does not yet help with multi-line defines
    m = re.search(
        r"^#define.*{}$".format(match.group(0)),
        body[: match.end()],
        re.MULTILINE,
    )
    return m is not None


def update_file(contents, namespaces):
    """Scan the contents of a file, and for top-level namespaces in namespaces remove redundant usages."""
    output = ""
    while contents:
        m = re.search(r"namespace ([a-zA-Z0-9_]*) {", contents)
        if not m:
            output += contents
            break
        output += contents[: m.end()]
        contents = contents[m.end() :]
        end = find_closing_mustache(contents, 1)
        if end is None:
            print(
                "Failed to find closing mustache for namespace {}".format(
                    m.group(1)
                )
            )
            print("Remaining text:")
            print(contents)
            sys.exit(1)
        body = contents[:end]
        namespace = m.group(1)
        if namespace in namespaces:
            while body:
                # Find instances of 'namespace::'
                m = re.search(r"\b" + namespace + r"::\b", body)
                if not m:
                    break
                # Ignore instances of '::namespace::' -- these are usually meant to be there.
                if m.start() >= 2 and body[m.start() - 2 :].startswith("::"):
                    output += body[: m.end()]
                # Ignore #defines, since they may be used anywhere
                elif is_a_define_statement(m, body):
                    output += body[: m.end()]
                else:
                    output += body[: m.start()]
                body = body[m.end() :]
        output += body
        contents = contents[end:]
    return output


# self check before doing anything
_TEST = """
namespace bar {
    namespace baz {
    }
}
namespace foo {}
namespace foo {
    foo::a;
    ::foo::a;
}
"""
_TEST_EXPECTED = """
namespace bar {
    namespace baz {
    }
}
namespace foo {}
namespace foo {
    a;
    ::foo::a;
}
"""
output = update_file(_TEST, ["foo"])
if output != _TEST_EXPECTED:
    import difflib

    print("FAILED: self check")
    print(
        "\n".join(
            difflib.ndiff(_TEST_EXPECTED.splitlines(1), output.splitlines(1))
        )
    )
    sys.exit(1)

# Main loop.
Config = collections.namedtuple("Config", ["dirs", "namespaces"])

_CONFIGURATION = (Config(["src/core", "test/core"], ["grpc_core"]),)

changed = []

for config in _CONFIGURATION:
    for dir in config.dirs:
        for root, dirs, files in os.walk(dir):
            for file in files:
                if file.endswith(".cc") or file.endswith(".h"):
                    path = os.path.join(root, file)
                    try:
                        with open(path) as f:
                            contents = f.read()
                    except IOError:
                        continue
                    updated = update_file(contents, config.namespaces)
                    if updated != contents:
                        changed.append(path)
                        with open(os.path.join(root, file), "w") as f:
                            f.write(updated)

if changed:
    print("The following files were changed:")
    for path in changed:
        print("  " + path)
    sys.exit(1)
