#!/usr/bin/env python3
import pathlib
import sys

def get_option(go_package):
  return (
    'option go_package = "github.com/grpc/grpc/testctrl/genproto/{}";'.format(
      go_package))

def add_go_package(protodef_lines, go_package):
  new_protodef = []

  for line in protodef_lines:
      new_protodef.append(line)

      if line.startswith('package '):
        new_protodef.append('option go_package = "{}";'.format(go_package))

  return '\n'.join(new_protodef)


def rm_go_package(protodef_lines, go_package):
  new_protodef = []

  for line in protodef_lines:
      if line == 'option go_package = "{}";'.format(go_package):
        continue
      else:
        new_protodef.append(line)

  return '\n'.join(new_protodef)


def main(command, filename):
  path = pathlib.Path(filename).resolve()

  tree = str(path).split('src/proto/')[1].split('/' + path.name)[0]
  go_package = 'github.com/grpc/grpc/testctrl/genproto/{}'.format(tree)

  protodef = path.read_text()
  protodef_lines = path.read_text().split('\n')

  if command == 'fix':
    new_protodef = add_go_package(protodef_lines, go_package)
  elif command == 'break':
    new_protodef = rm_go_package(protodef_lines, go_package)

  path.write_text(new_protodef)


if __name__ == '__main__':
  command = sys.argv[1]
  filename = sys.argv[2]
  main(command, filename)
