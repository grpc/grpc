"""Buildgen expand filegroups plugin.

This takes the list of libs from our json dictionary,
and expands any and all filegroup.

"""


def excluded(filename, exclude_res):
  for r in exclude_res:
    if r.search(filename):
      return True
  return False


def mako_plugin(dictionary):
  """The exported plugin code for expand_filegroups.

  The list of libs in the build.json file can contain "filegroups" tags.
  These refer to the filegroups in the root object. We will expand and
  merge filegroups on the src, headers and public_headers properties.

  """
  libs = dictionary.get('libs')
  filegroups_list = dictionary.get('filegroups')
  filegroups = {}

  for fg in filegroups_list:
    filegroups[fg['name']] = fg

  for lib in libs:
    for fg_name in lib.get('filegroups', []):
      fg = filegroups[fg_name]

      src = lib.get('src', [])
      src.extend(fg.get('src', []))
      lib['src'] = src

      headers = lib.get('headers', [])
      headers.extend(fg.get('headers', []))
      lib['headers'] = headers

      public_headers = lib.get('public_headers', [])
      public_headers.extend(fg.get('public_headers', []))
      lib['public_headers'] = public_headers
