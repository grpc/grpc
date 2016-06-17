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


"""Simple Mako renderer.

Just a wrapper around the mako rendering library.

"""

import getopt
import imp
import os
import cPickle as pickle
import shutil
import sys


from mako.lookup import TemplateLookup
from mako.runtime import Context
from mako.template import Template
import bunch
import yaml


# Imports a plugin
def import_plugin(name):
  _, base_ex = os.path.split(name)
  base, _ = os.path.splitext(base_ex)

  with open(name, 'r') as plugin_file:
    plugin_code = plugin_file.read()
  plugin_module = imp.new_module(base)
  exec plugin_code in plugin_module.__dict__
  return plugin_module


def out(msg):
  print >> sys.stderr, msg


def showhelp():
  out('mako-renderer.py [-o out] [-m cache] [-P preprocessed_input] [-d dict] [-d dict...]'
      ' [-t template] [-w preprocessed_output]')


def main(argv):
  got_input = False
  module_directory = None
  preprocessed_output = None
  dictionary = {}
  json_dict = {}
  got_output = False
  plugins = []
  output_name = None
  got_preprocessed_input = False
  output_merged = None

  try:
    opts, args = getopt.getopt(argv, 'hM:m:d:o:p:t:P:w:')
  except getopt.GetoptError:
    out('Unknown option')
    showhelp()
    sys.exit(2)

  for opt, arg in opts:
    if opt == '-h':
      out('Displaying showhelp')
      showhelp()
      sys.exit()
    elif opt == '-o':
      if got_output:
        out('Got more than one output')
        showhelp()
        sys.exit(3)
      got_output = True
      output_name = arg
    elif opt == '-m':
      if module_directory is not None:
        out('Got more than one cache directory')
        showhelp()
        sys.exit(4)
      module_directory = arg
    elif opt == '-M':
      if output_merged is not None:
        out('Got more than one output merged path')
        showhelp()
        sys.exit(5)
      output_merged = arg
    elif opt == '-P':
      assert not got_preprocessed_input
      assert json_dict == {}
      sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), 'plugins')))
      with open(arg, 'r') as dict_file:
        dictionary = pickle.load(dict_file)
      got_preprocessed_input = True
    elif opt == '-d':
      assert not got_preprocessed_input
      with open(arg, 'r') as dict_file:
        bunch.merge_json(json_dict, yaml.load(dict_file.read()))
    elif opt == '-p':
      plugins.append(import_plugin(arg))
    elif opt == '-w':
      preprocessed_output = arg

  if not got_preprocessed_input:
    for plugin in plugins:
      plugin.mako_plugin(json_dict)
    if output_merged:
      with open(output_merged, 'w') as yaml_file:
        yaml_file.write(yaml.dump(json_dict))
    for k, v in json_dict.items():
      dictionary[k] = bunch.to_bunch(v)

  if preprocessed_output:
    with open(preprocessed_output, 'w') as dict_file:
      pickle.dump(dictionary, dict_file)

  cleared_dir = False
  for arg in args:
    got_input = True
    with open(arg) as f:
      srcs = list(yaml.load_all(f.read()))
    for src in srcs:
      if isinstance(src, basestring):
        assert len(srcs) == 1
        template = Template(src,
                            filename=arg,
                            module_directory=module_directory,
                            lookup=TemplateLookup(directories=['.']))
        with open(output_name, 'w') as output_file:
          template.render_context(Context(output_file, **dictionary))
      else:
        # we have optional control data: this template represents
        # a directory
        if not cleared_dir:
          if not os.path.exists(output_name):
            pass
          elif os.path.isfile(output_name):
            os.unlink(output_name)
          else:
            shutil.rmtree(output_name, ignore_errors=True)
          cleared_dir = True
        items = []
        if 'foreach' in src:
          for el in dictionary[src['foreach']]:
            if 'cond' in src:
              args = dict(dictionary)
              args['selected'] = el
              if not eval(src['cond'], {}, args):
                continue
            items.append(el)
          assert items
        else:
          items = [None]
        for item in items:
          args = dict(dictionary)
          args['selected'] = item
          item_output_name = os.path.join(
              output_name, Template(src['output_name']).render(**args))
          if not os.path.exists(os.path.dirname(item_output_name)):
            os.makedirs(os.path.dirname(item_output_name))
          template = Template(src['template'],
                              filename=arg,
                              module_directory=module_directory,
                              lookup=TemplateLookup(directories=['.']))
          with open(item_output_name, 'w') as output_file:
            template.render_context(Context(output_file, **args))

  if not got_input and not preprocessed_output:
    out('Got nothing to do')
    showhelp()

if __name__ == '__main__':
  main(sys.argv[1:])
