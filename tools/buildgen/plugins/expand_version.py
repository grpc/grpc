# Copyright 2016, Google Inc.
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

"""Buildgen package version plugin

This parses the list of targets from the yaml build file, and creates
a custom version string for each language's package.

"""


import re

LANGUAGES = [
  'core', 
  'cpp', 
  'csharp', 
  'node', 
  'objc', 
  'php',
  'python', 
  'ruby', 
  ]

class Version:

  def __init__(self, s):
    self.tag = None
    if '-' in s:
      s, self.tag = s.split('-')
    self.major, self.minor, self.patch = [int(x) for x in s.split('.')]

  def __str__(self):
    """Version string in a somewhat idiomatic style for most languages"""
    s = '%d.%d.%d' % (self.major, self.minor, self.patch)
    if self.tag:
      s += '-%s' % self.tag
    return s

  def pep440(self):
    """Version string in Python PEP440 style"""
    s = '%d.%d.%d' % (self.major, self.minor, self.patch)
    if self.tag:
      # we need to translate from grpc version tags to pep440 version
      # tags; this code is likely to be a little ad-hoc
      if self.tag == 'dev':
        s += '.dev0'
      elif len(self.tag) >= 3 and self.tag[0:3] == 'pre':
        s += 'rc%d' % int(self.tag[3:])
      else:
        raise Exception('Don\'t know how to translate version tag "%s" to pep440' % self.tag)
    return s

  def ruby(self):
    """Version string in Ruby style"""
    if self.tag:
      return '%d.%d.%d.%s' % (self.major, self.minor, self.patch, self.tag)
    else:
      return '%d.%d.%d' % (self.major, self.minor, self.patch)

  def php(self):
    """Version string in PHP style"""
    """PECL does not allow tag in version string"""
    return '%d.%d.%d' % (self.major, self.minor, self.patch)

def mako_plugin(dictionary):
  """Expand version numbers:
     - for each language, ensure there's a language_version tag in
       settings (defaulting to the master version tag)
     - expand version strings to major, minor, patch, and tag
  """

  settings = dictionary['settings']
  master_version = Version(settings['version'])
  settings['version'] = master_version
  for language in LANGUAGES:
    version_tag = '%s_version' % language
    if version_tag in settings:
      settings[version_tag] = Version(settings[version_tag])
    else:
      settings[version_tag] = master_version
