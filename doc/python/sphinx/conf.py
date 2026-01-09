# Copyright 2018 The gRPC Authors
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

# -- Path setup --------------------------------------------------------------

import os
import sys

# Add all packages to sys.path
PYTHON_FOLDER = os.path.join(os.path.dirname(os.path.realpath(__file__)), '..',
                             '..', '..', 'src', 'python')
ALL_PACKAGES = [
    'grpcio', 'grpcio_admin', 'grpcio_channelz', 'grpcio_csds',
    'grpcio_health_checking', 'grpcio_observability',
    'grpcio_reflection', 'grpcio_status', 'grpcio_testing'
]
for pkg in ALL_PACKAGES:
    # Use .append() instead of insert(0) to let Sphinx find the compiled
    # Cython extensions from site-packages correctly
    sys.path.append(os.path.join(PYTHON_FOLDER, pkg))


# -- Project information -----------------------------------------------------

project = 'gRPC Python'
copyright = '2025, The gRPC Authors'
author = 'The gRPC Authors'

# Import generated grpc_version after the path been modified
import grpc_version
version = '.'.join(grpc_version.VERSION.split('.')[:3])
release = grpc_version.VERSION
if 'dev' in grpc_version.VERSION:
    branch = 'master'
else:
    branch = 'v%s.%s.x' % tuple(grpc_version.VERSION.split('.')[:2])

# -- General configuration ---------------------------------------------------

templates_path = ['_templates']
source_suffix = ['.rst', '.md']
master_doc = 'index'
language = 'en'
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']
pygments_style = None

# --- Extensions Configuration -----------------------------------------------

extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.viewcode',
    'sphinx.ext.todo',
    'sphinx.ext.napoleon',
    'sphinx.ext.coverage',
    'sphinx.ext.autodoc.typehints',
]

napoleon_google_docstring = True
napoleon_numpy_docstring = True
napoleon_include_special_with_doc = True

autodoc_default_options = {
    'members': None,
}

autodoc_mock_imports = ["envoy"]

autodoc_typehints = 'description'

# -- HTML Configuration -------------------------------------------------

html_theme = 'pydata_sphinx_theme'
html_title = f"gRPC Python Docs v{version}"
html_theme_options = {
    "navbar_center": [],
    "header_links_before_dropdown": 15,
    "secondary_sidebar_items": ["page-toc", "edit-this-page"],
    "show_toc_level": 2,
}

html_theme_options["analytics"] = {
    "google_analytics_id": "UA-60127042-1",
}

html_sidebars = {
    "**": ["sidebar-nav-bs"]
}

html_static_path = ["_static"]
html_css_files = [
    "custom.css"
]

# -- Options for manual page output ------------------------------------------

man_pages = [(master_doc, 'grpcio', 'grpcio Documentation', [author], 1)]

# -- Options for Texinfo output ----------------------------------------------

texinfo_documents = [
    (master_doc, 'grpcio', 'grpcio Documentation', author, 'grpcio',
     'One line description of project.', 'Miscellaneous'),
]

# -- Options for Epub output -------------------------------------------------

epub_title = project
epub_exclude_files = ['search.html']

# -- Options for todo extension ----------------------------------------------

todo_include_todos = True

# -- Options for substitutions -----------------------------------------------

rst_epilog = '.. |channel_arg_names_link| replace:: https://github.com/grpc/grpc/blob/%s/include/grpc/impl/channel_arg_names.h' % branch
