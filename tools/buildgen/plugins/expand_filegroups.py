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

"""Buildgen expand filegroups plugin.

This takes the list of libs from our yaml dictionary,
and expands any and all filegroup.

"""


def excluded(filename, exclude_res):
  for r in exclude_res:
    if r.search(filename):
      return True
  return False


def uniquify(lst):
  out = []
  for el in lst:
    if el not in out:
      out.append(el)
  return out


FILEGROUP_LISTS = ['src', 'headers', 'public_headers', 'deps']


FILEGROUP_DEFAULTS = {
  'language': 'c',
  'boringssl': False,
  'zlib': False,
}


def mako_plugin(dictionary):
  """The exported plugin code for expand_filegroups.

  The list of libs in the build.yaml file can contain "filegroups" tags.
  These refer to the filegroups in the root object. We will expand and
  merge filegroups on the src, headers and public_headers properties.

  """
  libs = dictionary.get('libs')
  targets = dictionary.get('targets')
  filegroups_list = dictionary.get('filegroups')
  filegroups = {}

  for fg in filegroups_list:
    for lst in FILEGROUP_LISTS:
      fg[lst] = fg.get(lst, [])
      fg['own_%s' % lst] = list(fg[lst])
    for attr, val in FILEGROUP_DEFAULTS.iteritems():
      if attr not in fg:
        fg[attr] = val

  todo = list(filegroups_list)
  skips = 0

  while todo:
    assert skips != len(todo), "infinite loop in filegroup uses clauses"
    # take the first element of the todo list
    cur = todo[0]
    todo = todo[1:]
    # check all uses filegroups are present (if no, skip and come back later)
    skip = False
    for uses in cur.get('uses', []):
      if uses not in filegroups:
        skip = True
    if skip:
      skips += 1
      todo.append(cur)
    else:
      skips = 0
      assert 'plugins' not in cur
      plugins = []
      for uses in cur.get('uses', []):
        for plugin in filegroups[uses]['plugins']:
          if plugin not in plugins:
            plugins.append(plugin)
        for lst in FILEGROUP_LISTS:
          vals = cur.get(lst, [])
          vals.extend(filegroups[uses].get(lst, []))
          cur[lst] = vals
      cur_plugin_name = cur.get('plugin')
      if cur_plugin_name:
        plugins.append(cur_plugin_name)
      cur['plugins'] = plugins
      filegroups[cur['name']] = cur

  # build reverse dependency map
  things = {}
  for thing in dictionary['libs'] + dictionary['targets'] + dictionary['filegroups']:
    things[thing['name']] = thing
    thing['used_by'] = []
  thing_deps = lambda t: t.get('uses', []) + t.get('filegroups', []) + t.get('deps', [])
  for thing in things.itervalues():
    done = set()
    todo = thing_deps(thing)
    while todo:
      cur = todo[0]
      todo = todo[1:]
      if cur in done: continue
      things[cur]['used_by'].append(thing['name'])
      todo.extend(thing_deps(things[cur]))
      done.add(cur)

  # the above expansion can introduce duplicate filenames: contract them here
  for fg in filegroups.itervalues():
    for lst in FILEGROUP_LISTS:
      fg[lst] = uniquify(fg.get(lst, []))

  for tgt in dictionary['targets']:
    for lst in FILEGROUP_LISTS:
      tgt[lst] = tgt.get(lst, [])
      tgt['own_%s' % lst] = list(tgt[lst])

  for lib in libs + targets:
    assert 'plugins' not in lib
    plugins = []
    for lst in FILEGROUP_LISTS:
      vals = lib.get(lst, [])
      lib[lst] = list(vals)
      lib['own_%s' % lst] = list(vals)
    for fg_name in lib.get('filegroups', []):
      fg = filegroups[fg_name]
      for plugin in fg['plugins']:
        if plugin not in plugins:
          plugins.append(plugin)
      for lst in FILEGROUP_LISTS:
        vals = lib.get(lst, [])
        vals.extend(fg.get(lst, []))
        lib[lst] = vals
      lib['plugins'] = plugins
    if lib.get('generate_plugin_registry', False):
      lib['src'].append('src/core/plugin_registry/%s_plugin_registry.c' %
                        lib['name'])
    for lst in FILEGROUP_LISTS:
      lib[lst] = uniquify(lib.get(lst, []))
