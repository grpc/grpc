#!/usr/bin/env python3

# Copyright 2026 gRPC authors.
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

### Verifies that every pinned package in requirements.bazel.lock has at least
### one --hash=sha256: line in its block. This is the structural invariant
### `pip install --require-hashes` requires and that rules_python's pip.parse
### treats as a precondition for hermetic Bazel builds.

import os
import re
import sys

LOCK_PATH = "requirements.bazel.lock"

# A package-start line is column 0, name (with optional [extras]) followed by
# ==version, possibly trailing whitespace and a backslash continuation.
PACKAGE_START = re.compile(
    r"^[A-Za-z][\w.\-]*(?:\[[\w,\-]+\])?==\S+\s*\\?\s*$"
)

# A hash line is indented and matches --hash=sha256:<64 hex chars>.
HASH_LINE = re.compile(r"^\s+--hash=sha256:[a-fA-F0-9]{64}\b")


def main():
    os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))
    missing = []
    current = None
    saw_hash = False

    with open(LOCK_PATH, "r") as f:
        for lineno, line in enumerate(f, start=1):
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            if line[0] in (" ", "\t"):
                if HASH_LINE.match(line):
                    saw_hash = True
                continue
            if not PACKAGE_START.match(line):
                sys.stderr.write(
                    f"{LOCK_PATH}:{lineno}: unexpected content: "
                    f"{line.rstrip()!r}\n"
                )
                return 1
            if current is not None and not saw_hash:
                missing.append(current)
            current = (lineno, stripped.rstrip(" \\"))
            saw_hash = False

    if current is not None and not saw_hash:
        missing.append(current)

    if missing:
        sys.stderr.write(
            f"{LOCK_PATH}: {len(missing)} package(s) missing "
            f"--hash=sha256: lines. This breaks the hermeticity contract of "
            f"pip.parse; regenerate with --generate-hashes (see the docstring "
            f"at the top of requirements.bazel.txt for the recipe).\n"
        )
        for lineno, pkg in missing:
            sys.stderr.write(f"  {LOCK_PATH}:{lineno}: {pkg}\n")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
