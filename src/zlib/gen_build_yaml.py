#!/usr/bin/env python2.7
import re
import os
import sys
import yaml

os.chdir(os.path.dirname(sys.argv[0])+'/../..')

out = {}

try:
  with open('third_party/zlib/CMakeLists.txt') as f:
    cmake = f.read()

  def cmpath(x):
    return 'third_party/zlib/%s' % x.replace('${CMAKE_CURRENT_BINARY_DIR}/', '')

  def cmvar(name):
    regex = r'set\(\s*'
    regex += name
    regex += r'([^)]*)\)'
    return [cmpath(x) for x in re.search(regex, cmake).group(1).split()]

  out['libs'] = [{
      'name': 'z',
      'zlib': True,
      'build': 'private',
      'language': 'c',
      'secure': 'no',
      'src': sorted(cmvar('ZLIB_SRCS')),
      'headers': sorted(cmvar('ZLIB_PUBLIC_HDRS') + cmvar('ZLIB_PRIVATE_HDRS')),
  }]
except:
  pass

print yaml.dump(out)

