# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CTestCoverageCollectGCOV
------------------------

.. versionadded:: 3.2

This module provides the ``ctest_coverage_collect_gcov`` function.

This function runs gcov on all .gcda files found in the binary tree
and packages the resulting .gcov files into a tar file.
This tarball also contains the following:

* *data.json* defines the source and build directories for use by CDash.
* *Labels.json* indicates any :prop_sf:`LABELS` that have been set on the
  source files.
* The *uncovered* directory holds any uncovered files found by
  :variable:`CTEST_EXTRA_COVERAGE_GLOB`.

After generating this tar file, it can be sent to CDash for display with the
:command:`ctest_submit(CDASH_UPLOAD)` command.

.. command:: ctest_coverage_collect_gcov

  ::

    ctest_coverage_collect_gcov(TARBALL <tarfile>
      [SOURCE <source_dir>][BUILD <build_dir>]
      [GCOV_COMMAND <gcov_command>]
      [GCOV_OPTIONS <options>...]
      )

  Run gcov and package a tar file for CDash.  The options are:

  ``TARBALL <tarfile>``
    Specify the location of the ``.tar`` file to be created for later
    upload to CDash.  Relative paths will be interpreted with respect
    to the top-level build directory.

  ``TARBALL_COMPRESSION <option>`` Specify a compression algorithm for the
    ``TARBALL`` data file.  Using this option reduces the size of the data file
    before it is submitted to CDash.  ``<option>`` must be one of ``GZIP``,
    ``BZIP2``, ``XZ``, ``ZSTD``, ``FROM_EXT``, or an expression that CMake
    evaluates as ``FALSE``. The default value is ``BZIP2``.

    If ``FROM_EXT`` is specified, the resulting file will be compressed based on
    the file extension of the ``<tarfile>`` (i.e. ``.tar.gz`` will use ``GZIP``
    compression). File extensions that will produce compressed output include
    ``.tar.gz``, ``.tgz``, ``.tar.bzip2``, ``.tbz``, ``.tar.xz``, and ``.txz``.

  ``SOURCE <source_dir>``
    Specify the top-level source directory for the build.
    Default is the value of :variable:`CTEST_SOURCE_DIRECTORY`.

  ``BUILD <build_dir>``
    Specify the top-level build directory for the build.
    Default is the value of :variable:`CTEST_BINARY_DIRECTORY`.

  ``GCOV_COMMAND <gcov_command>``
    Specify the full path to the ``gcov`` command on the machine.
    Default is the value of :variable:`CTEST_COVERAGE_COMMAND`.

  ``GCOV_OPTIONS <options>...``
    Specify options to be passed to gcov.  The ``gcov`` command
    is run as ``gcov <options>... -o <gcov-dir> <file>.gcda``.
    If not specified, the default option is just ``-b -x``.

  ``GLOB``
    Recursively search for .gcda files in build_dir rather than
    determining search locations by reading TargetDirectories.txt.

  ``DELETE``
    Delete coverage files after they've been packaged into the .tar.

  ``QUIET``
    Suppress non-error messages that otherwise would have been
    printed out by this function.
#]=======================================================================]

function(ctest_coverage_collect_gcov)
  set(options QUIET GLOB DELETE)
  set(oneValueArgs TARBALL SOURCE BUILD GCOV_COMMAND TARBALL_COMPRESSION)
  set(multiValueArgs GCOV_OPTIONS)
  cmake_parse_arguments(GCOV  "${options}" "${oneValueArgs}"
    "${multiValueArgs}" "" ${ARGN} )
  if(NOT DEFINED GCOV_TARBALL)
    message(FATAL_ERROR
      "TARBALL must be specified. for ctest_coverage_collect_gcov")
  endif()
  if(NOT DEFINED GCOV_SOURCE)
    set(source_dir "${CTEST_SOURCE_DIRECTORY}")
  else()
    set(source_dir "${GCOV_SOURCE}")
  endif()
  if(NOT DEFINED GCOV_BUILD)
    set(binary_dir "${CTEST_BINARY_DIRECTORY}")
  else()
    set(binary_dir "${GCOV_BUILD}")
  endif()
  if(NOT DEFINED GCOV_GCOV_COMMAND)
    set(gcov_command "${CTEST_COVERAGE_COMMAND}")
  else()
    set(gcov_command "${GCOV_GCOV_COMMAND}")
  endif()
  if(NOT DEFINED GCOV_TARBALL_COMPRESSION)
    set(GCOV_TARBALL_COMPRESSION "BZIP2")
  elseif( GCOV_TARBALL_COMPRESSION AND
      NOT GCOV_TARBALL_COMPRESSION MATCHES "^(GZIP|BZIP2|XZ|ZSTD|FROM_EXT)$")
    message(FATAL_ERROR "TARBALL_COMPRESSION must be one of OFF, GZIP, "
      "BZIP2, XZ, ZSTD, or FROM_EXT for ctest_coverage_collect_gcov")
  endif()
  # run gcov on each gcda file in the binary tree
  set(gcda_files)
  set(label_files)
  if (GCOV_GLOB)
      file(GLOB_RECURSE gfiles "${binary_dir}/*.gcda")
      list(LENGTH gfiles len)
      # if we have gcda files then also grab the labels file for that target
      if(${len} GREATER 0)
        file(GLOB_RECURSE lfiles RELATIVE ${binary_dir} "${binary_dir}/Labels.json")
        list(APPEND gcda_files ${gfiles})
        list(APPEND label_files ${lfiles})
      endif()
  else()
    # look for gcda files in the target directories
    # this will be faster and only look where the files will be
    file(STRINGS "${binary_dir}/CMakeFiles/TargetDirectories.txt" target_dirs
         ENCODING UTF-8)
    foreach(target_dir ${target_dirs})
      file(GLOB_RECURSE gfiles "${target_dir}/*.gcda")
      list(LENGTH gfiles len)
      # if we have gcda files then also grab the labels file for that target
      if(${len} GREATER 0)
        file(GLOB_RECURSE lfiles RELATIVE ${binary_dir}
          "${target_dir}/Labels.json")
        list(APPEND gcda_files ${gfiles})
        list(APPEND label_files ${lfiles})
      endif()
    endforeach()
  endif()
  # return early if no coverage files were found
  list(LENGTH gcda_files len)
  if(len EQUAL 0)
    if (NOT GCOV_QUIET)
      message("ctest_coverage_collect_gcov: No .gcda files found, "
        "ignoring coverage request.")
    endif()
    return()
  endif()
  # setup the dir for the coverage files
  set(coverage_dir "${binary_dir}/Testing/CoverageInfo")
  file(MAKE_DIRECTORY  "${coverage_dir}")
  # run gcov, this will produce the .gcov files in the current
  # working directory
  if(NOT DEFINED GCOV_GCOV_OPTIONS)
    set(GCOV_GCOV_OPTIONS -b -x)
  endif()
  if (GCOV_QUIET)
    set(coverage_out_opts
      OUTPUT_QUIET
      ERROR_QUIET
      )
  else()
    set(coverage_out_opts
      OUTPUT_FILE "${coverage_dir}/gcov.log"
      ERROR_FILE  "${coverage_dir}/gcov.log"
      )
  endif()
  execute_process(COMMAND
    ${gcov_command} ${GCOV_GCOV_OPTIONS} ${gcda_files}
    RESULT_VARIABLE res
    WORKING_DIRECTORY ${coverage_dir}
    ${coverage_out_opts}
    )

  if (GCOV_DELETE)
    file(REMOVE ${gcda_files})
  endif()

  if(NOT "${res}" EQUAL 0)
    if (NOT GCOV_QUIET)
      message(STATUS "Error running gcov: ${res}, see\n  ${coverage_dir}/gcov.log")
    endif()
  endif()
  # create json file with project information
  file(WRITE ${coverage_dir}/data.json
    "{
    \"Source\": \"${source_dir}\",
    \"Binary\": \"${binary_dir}\"
}")
  # collect the gcov files
  set(unfiltered_gcov_files)
  file(GLOB_RECURSE unfiltered_gcov_files RELATIVE ${binary_dir} "${coverage_dir}/*.gcov")

  # if CTEST_EXTRA_COVERAGE_GLOB was specified we search for files
  # that might be uncovered
  if (DEFINED CTEST_EXTRA_COVERAGE_GLOB)
    set(uncovered_files)
    foreach(search_entry IN LISTS CTEST_EXTRA_COVERAGE_GLOB)
      if(NOT GCOV_QUIET)
        message("Add coverage glob: ${search_entry}")
      endif()
      file(GLOB_RECURSE matching_files "${source_dir}/${search_entry}")
      if (matching_files)
        list(APPEND uncovered_files "${matching_files}")
      endif()
    endforeach()
  endif()

  set(gcov_files)
  foreach(gcov_file ${unfiltered_gcov_files})
    file(STRINGS ${binary_dir}/${gcov_file} first_line LIMIT_COUNT 1 ENCODING UTF-8)

    set(is_excluded false)
    if(first_line MATCHES "^        -:    0:Source:(.*)$")
      set(source_file ${CMAKE_MATCH_1})
    elseif(NOT GCOV_QUIET)
      message(STATUS "Could not determine source file corresponding to: ${gcov_file}")
    endif()

    foreach(exclude_entry IN LISTS CTEST_CUSTOM_COVERAGE_EXCLUDE)
      if(source_file MATCHES "${exclude_entry}")
        set(is_excluded true)

        if(NOT GCOV_QUIET)
          message("Excluding coverage for: ${source_file} which matches ${exclude_entry}")
        endif()

        break()
      endif()
    endforeach()

    get_filename_component(resolved_source_file "${source_file}" ABSOLUTE)
    foreach(uncovered_file IN LISTS uncovered_files)
      get_filename_component(resolved_uncovered_file "${uncovered_file}" ABSOLUTE)
      if (resolved_uncovered_file STREQUAL resolved_source_file)
        list(REMOVE_ITEM uncovered_files "${uncovered_file}")
      endif()
    endforeach()

    if(NOT is_excluded)
      list(APPEND gcov_files ${gcov_file})
    endif()
  endforeach()

  foreach (uncovered_file ${uncovered_files})
    # Check if this uncovered file should be excluded.
    set(is_excluded false)
    foreach(exclude_entry IN LISTS CTEST_CUSTOM_COVERAGE_EXCLUDE)
      if(uncovered_file MATCHES "${exclude_entry}")
        set(is_excluded true)
        if(NOT GCOV_QUIET)
          message("Excluding coverage for: ${uncovered_file} which matches ${exclude_entry}")
        endif()
        break()
      endif()
    endforeach()
    if(is_excluded)
      continue()
    endif()

    # Copy from source to binary dir, preserving any intermediate subdirectories.
    get_filename_component(filename "${uncovered_file}" NAME)
    get_filename_component(relative_path "${uncovered_file}" DIRECTORY)
    string(REPLACE "${source_dir}" "" relative_path "${relative_path}")
    if (relative_path)
      # Strip leading slash.
      string(SUBSTRING "${relative_path}" 1 -1 relative_path)
    endif()
    file(COPY ${uncovered_file} DESTINATION ${binary_dir}/uncovered/${relative_path})
    if(relative_path)
      list(APPEND uncovered_files_for_tar uncovered/${relative_path}/${filename})
    else()
      list(APPEND uncovered_files_for_tar uncovered/${filename})
    endif()
  endforeach()

  # tar up the coverage info with the same date so that the md5
  # sum will be the same for the tar file independent of file time
  # stamps
  string(REPLACE ";" "\n" gcov_files "${gcov_files}")
  string(REPLACE ";" "\n" label_files "${label_files}")
  string(REPLACE ";" "\n" uncovered_files_for_tar "${uncovered_files_for_tar}")
  file(WRITE "${coverage_dir}/coverage_file_list.txt"
    "${gcov_files}
${coverage_dir}/data.json
${label_files}
${uncovered_files_for_tar}
")

  # Prepare tar command line arguments

  set(tar_opts "")
  # Select data compression mode
  if( GCOV_TARBALL_COMPRESSION STREQUAL "FROM_EXT")
    if( GCOV_TARBALL MATCHES [[\.(tgz|tar.gz)$]] )
      string(APPEND tar_opts "z")
    elseif( GCOV_TARBALL MATCHES [[\.(txz|tar.xz)$]] )
      string(APPEND tar_opts "J")
    elseif( GCOV_TARBALL MATCHES [[\.(tbz|tar.bz)$]] )
      string(APPEND tar_opts "j")
    endif()
  elseif(GCOV_TARBALL_COMPRESSION STREQUAL "GZIP")
    string(APPEND tar_opts "z")
  elseif(GCOV_TARBALL_COMPRESSION STREQUAL "XZ")
    string(APPEND tar_opts "J")
  elseif(GCOV_TARBALL_COMPRESSION STREQUAL "BZIP2")
    string(APPEND tar_opts "j")
  elseif(GCOV_TARBALL_COMPRESSION STREQUAL "ZSTD")
    set(zstd_tar_opt "--zstd")
  endif()
  # Verbosity options
  if(NOT GCOV_QUIET AND NOT tar_opts MATCHES v)
    string(APPEND tar_opts "v")
  endif()
  # Prepend option 'c' specifying 'create'
  string(PREPEND tar_opts "c")
  # Append option 'f' so that the next argument is the filename
  string(APPEND tar_opts "f")

  execute_process(COMMAND
    ${CMAKE_COMMAND} -E tar ${tar_opts} ${GCOV_TARBALL} ${zstd_tar_opt}
    "--mtime=1970-01-01 0:0:0 UTC"
    "--format=gnutar"
    --files-from=${coverage_dir}/coverage_file_list.txt
    WORKING_DIRECTORY ${binary_dir})

  if (GCOV_DELETE)
    foreach(gcov_file ${unfiltered_gcov_files})
      file(REMOVE ${binary_dir}/${gcov_file})
    endforeach()
    file(REMOVE ${coverage_dir}/coverage_file_list.txt)
    file(REMOVE ${coverage_dir}/data.json)
    if (EXISTS ${binary_dir}/uncovered)
      file(REMOVE ${binary_dir}/uncovered)
    endif()
  endif()

endfunction()
