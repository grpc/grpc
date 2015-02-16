#!/usr/bin/python2.7
import os
import sys
import subprocess

# find our home
ROOT = os.path.abspath(
    os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

# open the license text
with open('LICENSE') as f:
  LICENSE = f.read().splitlines()

# license format by file extension
# key is the file extension, value is a format string
# that given a line of license text, returns what should
# be in the file
LICENSE_FMT = {
  '.c': ' * %s',
  '.cc': ' * %s',
  '.h': ' * %s',
}

# pregenerate the actual text that we should have
LICENSE_TEXT = dict(
    (k, '\n'.join((v % line).rstrip() for line in LICENSE))
    for k, v in LICENSE_FMT.iteritems())

OLD_LICENSE_TEXT = dict(
    (k, v.replace('2015', '2014')) for k, v in LICENSE_TEXT.iteritems())

# scan files, validate the text
for filename in subprocess.check_output('git ls-tree -r --name-only -r HEAD',
                                        shell=True).splitlines():
  ext = os.path.splitext(filename)[1]
  if ext not in LICENSE_TEXT: continue
  license = LICENSE_TEXT[ext]
  old_license = OLD_LICENSE_TEXT[ext]
  with open(filename) as f:
    text = '\n'.join(line.rstrip() for line in f.read().splitlines())
  if license in text:
    pass
  elif old_license in text:
    pass
    #print 'old license in: %s' % filename
  else:
    print 'no license in: %s' % filename

