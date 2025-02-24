#! /bin/bash
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


set -e

export TEST=${TEST:-false}

YAML_OK=$(python3 -c "import yaml; print(yaml.__version__.split('.') >= ['5', '4', '1'])")

if [[ "${YAML_OK}" != "True" ]]; then
  # PyYAML dropped 3.5 support at 5.4.1, which makes 5.3.1 the latest version we
  # can use.
  python3 -m pip install --upgrade --ignore-installed PyYAML==5.3.1 --user
fi

cd `dirname $0`/../..

echo "Generating build_autogenerated.yaml from bazel BUILD file"
rm -f build_autogenerated.yaml
python3 tools/buildgen/extract_metadata_from_bazel_xml.py

tools/buildgen/build_cleaner.py build_handwritten.yaml

# /usr/local/google/home/rbellevi/dev/tmp/grpc/venv/bin/python3: No module named virtualenv
# Generate xds-protos
if [[ ! -d generate_projects_virtual_environment ]]; then
  if ! python3 -m pip freeze | grep virtualenv &>/dev/null; then
    echo "virtualenv Python module not installed. Attempting to install via pip." >/dev/stderr
    if INSTALL_OUTPUT=$(! python3 -m pip install virtualenv --upgrade &>/dev/stdout); then
      echo "$INSTALL_OUTPUT"
      if echo "$INSTALL_OUTPUT" | grep "externally managed" &>/dev/null; then
        echo >/dev/stderr
        echo "############################" >/dev/stderr
        echo  "Your administrator is _insisting_ on managing your packages themself. Try running \`sudo apt-get install python3-virtualenv\`" >/dev/stderr
        echo "############################" >/dev/stderr
      fi
      exit 1
    fi
  fi 
  python3 -m virtualenv generate_projects_virtual_environment
fi

generate_projects_virtual_environment/bin/pip install --upgrade --ignore-installed grpcio-tools==1.59.0
generate_projects_virtual_environment/bin/python tools/distrib/python/xds_protos/build.py
generate_projects_virtual_environment/bin/python tools/distrib/python/make_grpcio_tools.py
generate_projects_virtual_environment/bin/python src/python/grpcio_observability/make_grpcio_observability.py

# check build_autogenerated.yaml is already in its "clean" form
TEST=true tools/buildgen/build_cleaner.py build_autogenerated.yaml

. tools/buildgen/generate_build_additions.sh

# Instead of generating from a single build.yaml, we've split it into
# - build_handwritten.yaml: manually written metadata
# - build_autogenerated.yaml: generated from bazel BUILD file
python3 tools/buildgen/generate_projects.py build_handwritten.yaml build_autogenerated.yaml $gen_build_files "$@"

rm $gen_build_files

tools/artifact_gen/artifact_gen.sh
