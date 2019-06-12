#!/bin/bash
# BuildUsingXCodeBuild.sh

# Copyright 2012 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy
# of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.

# Run this in a custom build phase in order to force compilation of the target
# to occur using xcodebuild. This is useful if you have a "Test All" target
# that depends on multiple unit test targets, each of which disable automatic
# running of unit tests when build from the IDE.

if [[ $TEST_AFTER_BUILD == YES ]]; then
  # Already being built with xcodebuild.
  exit 0
fi

# RUN_CLANG_STATIC_ANALYZER is set to NO since the build done in the IDE would
# have already analyzed the files.
# TEST_AFTER_BUILD is explicitly set to YES to ensure that when we build
# through xcodebuild we will not hit this point again. xcodebuild used to
# set this to YES itself, but starting with 4.3, it no longer sets it.
exec "${DEVELOPER_BIN_DIR}/xcodebuild" \
    -project "${PROJECT_FILE_PATH}" \
    -target "${TARGET_NAME}" \
    -sdk "${SDKROOT}" \
    -configuration "${CONFIGURATION}" \
    SDKROOT="${SDKROOT}"  \
    CONFIGURATION_BUILD_DIR="${CONFIGURATION_BUILD_DIR}" \
    CONFIGURATION_TEMP_DIR="${CONFIGURATION_TEMP_DIR}" \
    OBJROOT="${OBJROOT}" \
    SYMROOT="${SYMROOT}" \
    CACHE_ROOT="${CACHE_ROOT}" \
    ONLY_ACTIVE_ARCH="${ONLY_ACTIVE_ARCH}" \
    ARCHS="${ARCHS}" \
    RUN_CLANG_STATIC_ANALYZER=NO \
    TEST_AFTER_BUILD=YES \
    "$@"
