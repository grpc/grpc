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

# This script finds and eliminates redundant namespace qualifiers.
#
# It is optimized to run in parallel using all available CPU cores.
#
# USAGE:
#
# 1. To check the entire repository (the default CI behavior):
#    ./check_redundant_namespace_qualifiers.py
#
# 2. To check only specific files (e.g., files changed in your git branch):
#    git diff --name-only main... | xargs ./check_redundant_namespace_qualifiers.py
#

import collections
import multiprocessing
import os
import re
import sys

IGNORED_FILES = [
    # note: the grpc_core::Server redundant namespace qualification is required
    # for older gcc versions.
    "src/core/ext/transport/chttp2/server/chttp2_server.h",
    "src/core/server/server.h",
    # generated code adds a necessary grpc_core:: for a logging macro which can
    # be used anywhere.
    "src/core/lib/debug/trace_impl.h",
]


def find_closing_mustache(contents, initial_depth):
    """Find the closing mustache for a given number of open mustaches."""
    depth = initial_depth
    start_len = len(contents)
    while contents:
        # Skip over raw strings.
        if contents.startswith('R"'):
            contents = contents[2:]
            offset = contents.find("(")
            if offset == -1:
                return None  # Malformed raw string
            prefix = contents[:offset]
            contents = contents[offset:]
            end_marker = f'){prefix}"'
            offset = contents.find(end_marker)
            if offset == -1:
                return None  # Malformed raw string
            contents = contents[offset + len(end_marker) :]
        # Skip over strings.
        elif contents[0] == '"':
            contents = contents[1:]
            while len(contents) > 0 and contents[0] != '"':
                if contents.startswith(("\\\\", '\\"')):
                    contents = contents[2:]
                else:
                    contents = contents[1:]
            if len(contents) > 0:
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
            offset = contents.find("\n")
            if offset == -1:
                break
            contents = contents[offset:]
        elif contents.startswith("/*"):
            offset = contents.find("*/")
            if offset == -1:
                break
            contents = contents[offset + 2 :]
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
        r"^#define.*{}$".format(re.escape(match.group(0))),
        body[: match.end()],
        re.MULTILINE,
    )
    return m is not None


def update_file(contents, namespaces):
    """Scan the contents of a file, and for top-level namespaces in namespaces remove redundant usages."""
    parts = []
    while contents:
        m = re.search(r"namespace\s+([a-zA-Z0-9_]*)\s*{", contents)
        if not m:
            parts.append(contents)
            break
        parts.append(contents[: m.end()])
        contents = contents[m.end() :]
        end = find_closing_mustache(contents, 1)
        if end is None:
            # Could not find a match, return original content to be safe
            parts.append(contents)
            return "".join(parts)

        body = contents[:end]
        namespace = m.group(1)
        if namespace in namespaces:
            body_parts = []
            while body:
                # Find instances of 'namespace::'
                m = re.search(r"\b" + namespace + r"::\b", body)
                if not m:
                    break
                # Ignore instances of '::namespace::' -- these are usually meant to be there.
                if m.start() >= 2 and body[m.start() - 2 :].startswith("::"):
                    body_parts.append(body[: m.end()])
                # Ignore #defines, since they may be used anywhere
                elif is_a_define_statement(m, body):
                    body_parts.append(body[: m.end()])
                else:
                    body_parts.append(body[: m.start()])
                body = body[m.end() :]
            body_parts.append(body)
            parts.append("".join(body_parts))
        else:
            parts.append(body)

        contents = contents[end:]
    return "".join(parts)


def run_self_check():
    """Run a self-check before doing anything."""
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
    {R"foo({
    foo::a;
    }foo)"}
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
    {R"foo({
    foo::a;
    }foo)"}
    """
    output = update_file(_TEST, ["foo"])
    if output != _TEST_EXPECTED:
        import difflib

        print("FAILED: self check")
        print(
            "\n".join(
                difflib.ndiff(
                    _TEST_EXPECTED.splitlines(1), output.splitlines(1)
                )
            )
        )
        sys.exit(1)


# Define a configuration tuple for clarity.
Config = collections.namedtuple("Config", ["dirs", "namespaces"])
_CONFIGURATION = (Config(("src/core", "test/core"), ("grpc_core",)),)


def process_file(path_and_config):
    """
    Processes a single file. Reads, updates, and writes back if changed.
    Returns the file path if changed, otherwise None.
    This function is designed to be called from a multiprocessing Pool.
    """
    path, config = path_and_config
    if any(ignored in path for ignored in IGNORED_FILES):
        return None
    try:
        with open(path) as f:
            contents = f.read()
    except IOError:
        return None  # Skip files that can't be opened

    try:
        updated = update_file(contents, config.namespaces)
        if updated != contents:
            with open(path, "w") as f:
                f.write(updated)
            return path
    except Exception as e:
        print(f"ERROR processing {path}: {e}", file=sys.stderr)
    return None


def main():
    """Main loop."""
    run_self_check()

    # If file paths are passed as arguments, use them.
    # Otherwise, walk the directories defined in the configuration.
    args = sys.argv[1:]
    if args:
        # For scoped runs, we must determine the config for each file.
        files_to_check = []
        for path in args:
            for config in _CONFIGURATION:
                if any(path.startswith(d) for d in config.dirs):
                    files_to_check.append((path, config))
                    break
    else:
        # Prepare a list of all files to be checked.
        files_to_check = []
        for config in _CONFIGURATION:
            for directory in config.dirs:
                for root, _, files in os.walk(directory):
                    for file in files:
                        if file.endswith((".cc", ".h")):
                            path = os.path.join(root, file)
                            files_to_check.append((path, config))

    if not files_to_check:
        return

    print(f"Checking {len(files_to_check)} files for redundant namespaces...")

    # Use a multiprocessing Pool to process files in parallel.
    # The number of processes will default to the number of CPU cores.
    with multiprocessing.Pool() as pool:
        # imap_unordered is used for efficiency, processing results as they complete.
        results = pool.imap_unordered(process_file, files_to_check)
        changed = [result for result in results if result is not None]

    if changed:
        print("The following files were changed:")
        for path in sorted(changed):
            print(f"  {path}")
        sys.exit(1)
    else:
        print("No files needed changes.")


if __name__ == "__main__":
    main()
