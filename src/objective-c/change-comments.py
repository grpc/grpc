#!/usr/bin/python

import re
import sys


if len(sys.argv) != 2:
  print("Please provide a source file name as only argument.")
  quit()

print("Modifying format of {file} comments in place...".format(
    file = sys.argv[1],
))


# Input

lines = []

with open(sys.argv[1], "r") as input_file:
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
  with open(sys.argv[1], "w") as otuput_file:
    for line in output_lines:
      otuput_file.write(line)


# Pattern matching

comment_regex = r'^(\s*)//\s(.*)$'

def is_comment(line):
  return re.search(comment_regex, line)

def isnt_comment(line):
  return not is_comment(line)

def next_line(predicate):
  if not more_input_available():
    return False
  return predicate(peek())


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
  return [indent + line + "\n" for line in block]


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
