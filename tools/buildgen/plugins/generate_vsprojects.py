"""Buildgen vsprojects plugin.

This parses the list of libraries, and generates globals "vsprojects"
and "vsproject_dict", to be used by the visual studio generators.

"""


import re


def mako_plugin(dictionary):
  """The exported plugin code for generate_vsprojeccts

  We want to help the work of the visual studio generators.

  """

  libs = dictionary.get('libs', [])
  targets = dictionary.get('targets', [])

  for lib in libs:
    lib['is_library'] = True
  for target in targets:
    target['is_library'] = False

  projects = []
  projects.extend(libs)
  projects.extend(targets)
  projects = [project for project in projects if project.get('vs_project_guid', None)]

  ## Exclude C++ projects for now
  projects = [project for project in projects if not project.get('c++', False)]

  project_dict = dict([(p['name'], p) for p in projects])

  dictionary['vsprojects'] = projects
  dictionary['vsproject_dict'] = project_dict
