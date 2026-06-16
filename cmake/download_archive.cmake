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
function(download_archive destination url fallback_url hash strip_prefix)
  # Fetch and validate
  set(_TEMPORARY_FILE ${_download_archive_TEMPORARY_DIR}/${strip_prefix}.tar.gz)
  set(_download_SUCCESS FALSE)
  set(_download_ATTEMPT 0)
  set(_download_MAX_ATTEMPTS 3)
  while(NOT _download_SUCCESS AND _download_ATTEMPT LESS _download_MAX_ATTEMPTS)
    math(EXPR _download_ATTEMPT "${_download_ATTEMPT} + 1")
    message(STATUS "Downloading from ${url} (Attempt ${_download_ATTEMPT} of ${_download_MAX_ATTEMPTS})")
    file(DOWNLOAD ${url} ${_TEMPORARY_FILE}
         TIMEOUT 60
         EXPECTED_HASH SHA256=${hash}
         TLS_VERIFY ON
         STATUS _download_STATUS)

    list(GET _download_STATUS 0 _download_STATUS_CODE)

    if (NOT _download_STATUS_CODE EQUAL 0)
      message(STATUS "Downloading from fallback ${fallback_url} (Attempt ${_download_ATTEMPT} of ${_download_MAX_ATTEMPTS})")
      file(DOWNLOAD ${fallback_url} ${_TEMPORARY_FILE}
          TIMEOUT 60
          EXPECTED_HASH SHA256=${hash}
          TLS_VERIFY ON
          STATUS _download_STATUS)

      list(GET _download_STATUS 0 _download_STATUS_CODE)
    endif()

    if(_download_STATUS_CODE EQUAL 0)
      set(_download_SUCCESS TRUE)
    else()
      list(GET _download_STATUS 1 _download_STATUS_MESSAGE)
      message(WARNING "Download failed (Attempt ${_download_ATTEMPT}): ${_download_STATUS_MESSAGE}")
      if(_download_ATTEMPT LESS _download_MAX_ATTEMPTS)
        message(STATUS "Retrying in 5 seconds...")
        execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 5)
      endif()
    endif()
  endwhile()

  if(NOT _download_SUCCESS)
    message(FATAL_ERROR "Failed to download from ${url} (fallback: ${fallback_url}) after ${_download_MAX_ATTEMPTS} attempts.")
  endif()
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
