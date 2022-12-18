#!/usr/bin/env python3

# Copyright 2022 gRPC authors.
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
import os
import sys
import tempfile

DEBUG = False


def convert(in_file, out_file):
    inside_c_comment = False
    comment_indent = 0
    comment_style = "//"

    for line in in_file:
        line = line.rstrip()
        if DEBUG:
            print(line)
        out_line = ""

        comment_start = False
        inside_string = False
        # new line starts with inside indentation
        inside_indentation = True
        k = 0
        while k < len(line):
            if inside_string:
                if line[k] == '"' and (k == 0 or line[k - 1] != "\\"):
                    inside_string = False
                out_line += line[k]
                k += 1
            else:
                if inside_c_comment:
                    if inside_indentation:
                        if (k >= comment_indent) or not (line[k] in [" ", "\t"]):
                            # emit C++ style comment
                            inside_indentation = False
                            out_line += comment_style
                            # Consume up to len(comment_style) chars from the line.
                            for m in range(k, min(k + len(comment_style), len(line))):
                                if line[m] in [" ", "\t"]:
                                    k += 1
                                elif line[m] == "*" and (
                                    (m + 1) == len(line) or line[m + 1] != "/"
                                ):
                                    k += 1
                                else:
                                    break
                    if k < len(line):
                        if (
                            line[k] == "*"
                            and (k + 1) < len(line)
                            and line[k + 1] == "/"
                        ):
                            inside_c_comment = False
                            # might be C style argument comment
                            if (k + 2) < len(line) and comment_start:
                                assert comment_style == "//"
                                # revert
                                out_line = (
                                    out_line[: comment_indent + 1]
                                    + "*"
                                    + out_line[comment_indent + 2 :]
                                )
                                out_line += "*/"
                            k += 2
                        else:
                            out_line += line[k]
                            k += 1
                else:
                    if line[k] == "/" and (k + 1) < len(line) and line[k + 1] == "*":
                        # Start of C style comment.
                        inside_c_comment = True
                        comment_indent = k
                        comment_style = "//"
                        comment_start = True
                        k += 2
                        if k < len(line) and (line[k] == "*" or line[k] == "!"):
                            if (k + 1) >= len(line) or line[k + 1] != "*":
                                # Start of Doxygen comment.
                                comment_style = "///"
                                k += 1
                        out_line += comment_style
                        inside_indentation = False
                    elif line[k] == "/" and (k + 1) < len(line) and line[k + 1] == "/":
                        # Start of C++ style comment.
                        out_line += line[k:]
                        k = len(line)
                    else:
                        if line[k] == '"':
                            inside_string = True
                        out_line += line[k]
                        k += 1

        out_file.write(f"{out_line}\n")


def main():
    # Handle the program arguments.
    parser = argparse.ArgumentParser(
        description="Convert C-style comments to C++-style."
    )
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="modify the input file in-place",
    )
    parser.add_argument("infile", nargs="?", help="input file (default: stdin)")
    parser.add_argument("outfile", nargs="?", help="output file (default: stdout)")
    args = parser.parse_args()

    # Select input and output files.
    in_file = sys.stdin
    # dry run by default
    out_file = sys.stdout
    if args.infile:
        in_file = open(args.infile, "r", encoding="utf8")
    temp_file_path = ""
    if args.in_place:
        (fd, temp_file_path) = tempfile.mkstemp()
        out_file = os.fdopen(fd, "w", encoding="utf8")
    elif args.outfile:
        out_file = open(args.outfile, "w", encoding="utf8")

    convert(in_file, out_file)

    if args.in_place:
        # clobber move
        os.rename(temp_file_path, args.infile)


if __name__ == "__main__":
    main()
