#!/usr/bin/python

import re
import sys

print 'Number of arguments:', len(sys.argv), 'arguments.'
print 'Argument List:', str(sys.argv)

with open(sys.argv[0], "r") as input_file:
  lines = input_file.readlines()

def peek():
  return lines[0]

def read_line():
  return lines.pop(0)

def more_input():
  return lines


comment_regex = r'^(\s*)//\s(.*)$'

def is_comment(line):
  return re.search(comment_regex, line)

def isnt_comment(line):
  return not is_comment(line)

def next_line(predicate):
  if not more_input():
    return False
  return predicate(peek())


output_lines = []

def output(line):
  output_lines.append(line)


while more_input():
  while next_line(isnt_comment):
    output(read_line())

  comment_block = []
  # Get all lines in the same comment block. We could restrict the indentation
  # to be the same as the first line of the block, but it's probably ok.
  while (next_line(is_comment)):
    comment_block.append(read_line())

  for line in format_as_block(comment_block):
    output(line)
