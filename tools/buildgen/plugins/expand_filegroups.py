# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
  'ares': False,
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
    assert skips != len(todo), "infinite loop in filegroup uses clauses: %r" % [t['name'] for t in todo]
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
