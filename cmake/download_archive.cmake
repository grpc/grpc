# Copyright 2021 The gRPC Authors
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

set(_download_archive_TEMPORARY_DIR ${CMAKE_BINARY_DIR}/http_archives)
file(MAKE_DIRECTORY ${_download_archive_TEMPORARY_DIR})

# This is basically Bazel's http_archive.
# Note that strip_prefix strips the directory path prefix of the extracted
# archive content, and it may strip multiple directories.
function(download_archive destination url hash strip_prefix)
  # Fetch and validate
  set(_TEMPORARY_FILE ${_download_archive_TEMPORARY_DIR}/${strip_prefix}.tar.gz)
  message(STATUS "Downloading from ${url}, if failed, please try configuring again")
  file(DOWNLOAD ${url} ${_TEMPORARY_FILE}
       TIMEOUT 60
       EXPECTED_HASH SHA256=${hash}
       TLS_VERIFY ON)
  # Extract
  execute_process(COMMAND
                  ${CMAKE_COMMAND} -E tar xvf ${_TEMPORARY_FILE}
                  WORKING_DIRECTORY ${_download_archive_TEMPORARY_DIR}
                  OUTPUT_QUIET)
  get_filename_component(_download_archive_Destination_Path ${destination} DIRECTORY)
  file(MAKE_DIRECTORY ${_download_archive_Destination_Path})
  file(RENAME ${_download_archive_TEMPORARY_DIR}/${strip_prefix} ${destination})
  # Clean up
  file(REMOVE ${_download_archive_TEMPORARY_DIR}/${strip_prefix})
  file(REMOVE ${_TEMPORARY_FILE})
endfunction()
