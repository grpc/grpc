#!/usr/bin/python

"""Simple Mako renderer.

Just a wrapper around the mako rendering library.

"""

import getopt
import imp
import os
import sys


from mako.lookup import TemplateLookup
from mako.runtime import Context
from mako.template import Template
import simplejson
import bunch


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
  out('mako-renderer.py [-o out] [-m cache] [-d dict] [-d dict...] template')


def main(argv):
  got_input = False
  module_directory = None
  dictionary = {}
  json_dict = {}
  got_output = False
  output_file = sys.stdout
  plugins = []

  try:
    opts, args = getopt.getopt(argv, 'hm:d:o:p:')
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
      output_file = open(arg, 'w')
    elif opt == '-m':
      if module_directory is not None:
        out('Got more than one cache directory')
        showhelp()
        sys.exit(4)
      module_directory = arg
    elif opt == '-d':
      dict_file = open(arg, 'r')
      bunch.merge_json(json_dict, simplejson.loads(dict_file.read()))
      dict_file.close()
    elif opt == '-p':
      plugins.append(import_plugin(arg))

  for plugin in plugins:
    plugin.mako_plugin(json_dict)

  for k, v in json_dict.items():
    dictionary[k] = bunch.to_bunch(v)

  ctx = Context(output_file, **dictionary)

  for arg in args:
    got_input = True
    template = Template(filename=arg,
                        module_directory=module_directory,
                        lookup=TemplateLookup(directories=['.']))
    template.render_context(ctx)

  if not got_input:
    out('Got nothing to do')
    showhelp()

  output_file.close()

if __name__ == '__main__':
  main(sys.argv[1:])
