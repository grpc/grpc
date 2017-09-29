#!/bin/bash
# Copyright 2017 gRPC authors.
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
#
# Generates cloudbuild.yaml, which GCR uses to determine which Dockerfiles
# to build when the gRPC repo is updated

set +ex

cd $(dirname $0)/../..

CLOUDBUILD_YAML=tools/dockerfile/cloudbuild.yaml
PROJECT_ID=grpc-testing

echo "steps:" > $CLOUDBUILD_YAML

for DOCKERFILE_DIR in tools/dockerfile/test/* tools/dockerfile/grpc_artifact_* tools/dockerfile/interoptest/* third_party/rake-compiler-dock
do
  echo "- name: 'gcr.io/cloud-builders/docker'" >> $CLOUDBUILD_YAML
  echo -e "  args: ['build', '-t', 'gcr.io/$PROJECT_ID/$DOCKERFILE_DIR', './$DOCKERFILE_DIR']\n" >> $CLOUDBUILD_YAML
done

echo "images:" >> $CLOUDBUILD_YAML

for DOCKERFILE_DIR in tools/dockerfile/test/* tools/dockerfile/grpc_artifact_* tools/dockerfile/interoptest/* third_party/rake-compiler-dock
do
  echo "- 'gcr.io/$PROJECT_ID/$DOCKERFILE_DIR'" >> $CLOUDBUILD_YAML
done

echo -e "\n# It can take a long time to upload images if many Dockerfiles are changed" >> $CLOUDBUILD_YAML
echo "timeout: 2880m" >> $CLOUDBUILD_YAML
