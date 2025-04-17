# Copyright 2024 gRPC authors.
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
"""Buildgen python version plugin

This parses the list of supported python versions from the yaml build file, and 
creates custom strings for the minimum and maximum supported python versions.

"""


def mako_plugin(dictionary):
    """Expand version numbers:
    - for each language, ensure there's a language_version tag in
      settings (defaulting to the master version tag)
    - expand version strings to major, minor, patch, and tag
    """

    settings = dictionary["settings"]

    supported_python_versions = settings["supported_python_versions"]
    settings["min_python_version"] = supported_python_versions[0]
    settings["max_python_version"] = supported_python_versions[-1]
