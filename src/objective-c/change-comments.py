#!/usr/bin/env python2.7
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Change comments style of source files from // to /** */"""

import re
import sys


if len(sys.argv) < 2:
  print("Please provide at least one source file name as argument.")
  sys.exit()

for file_name in sys.argv[1:]:

  print("Modifying format of {file} comments in place...".format(
      file=file_name,
  ))


  # Input

  with open(file_name, "r") as input_file:
    lines = input_file.readlines()

  def peek():
    return lines[0]

  def read_line():
    return lines.pop(0)

  def more_input_available():
    return lines


  # Output

  output_lines = []

  def write(line):
    output_lines.append(line)

  def flush_output():
    with open(file_name, "w") as output_file:
      for line in output_lines:
        output_file.write(line)


  # Pattern matching

  comment_regex = r'^(\s*)//\s(.*)$'

  def is_comment(line):
    return re.search(comment_regex, line)

  def isnt_comment(line):
    return not is_comment(line)

  def next_line(predicate):
    return more_input_available() and predicate(peek())


  # Transformation

  def indentation_of(line):
    match = re.search(comment_regex, line)
    return match.group(1)

  def content(line):
    match = re.search(comment_regex, line)
    return match.group(2)

  def format_as_block(comment_block):
    if len(comment_block) == 0:
      return []

    indent = indentation_of(comment_block[0])

    if len(comment_block) == 1:
      return [indent + "/** " + content(comment_block[0]) + " */\n"]

    block = ["/**"] + [" * " + content(line) for line in comment_block] + [" */"]
    return [indent + line.rstrip() + "\n" for line in block]


  # Main algorithm

  while more_input_available():
    while next_line(isnt_comment):
      write(read_line())

    comment_block = []
    # Get all lines in the same comment block. We could restrict the indentation
    # to be the same as the first line of the block, but it's probably ok.
    while (next_line(is_comment)):
      comment_block.append(read_line())

    for line in format_as_block(comment_block):
      write(line)

  flush_output()
