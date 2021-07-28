# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CMakeVerifyManifest
-------------------



CMakeVerifyManifest.cmake

This script is used to verify that embedded manifests and side by side
manifests for a project match.  To run this script, cd to a directory
and run the script with cmake -P.  On the command line you can pass in
versions that are OK even if not found in the .manifest files.  For
example, cmake -Dallow_versions=8.0.50608.0
-PCmakeVerifyManifest.cmake could be used to allow an embedded manifest
of 8.0.50608.0 to be used in a project even if that version was not
found in the .manifest file.
#]=======================================================================]

# This script first recursively globs *.manifest files from
# the current directory.  Then globs *.exe and *.dll.  Each
# .exe and .dll is scanned for embedded manifests and the versions
# of CRT are compared to those found in the .manifest files
# from the first glob.

# crt_version:
# function to extract the CRT version from a file
# this can be passed a .exe, .dll, or a .manifest file
# it will put the list of versions found into the variable
# specified by list_var
function(crt_version file list_var)
  file(STRINGS "${file}" strings REGEX "Microsoft.VC...CRT" NEWLINE_CONSUME)
  foreach(s ${strings})
    set(has_match 1)
    string(REGEX
      REPLACE ".*<assembly.*\"Microsoft.VC...CRT\".*version=\"([^\"]*)\".*</assembly>.*$" "\\1"
      version "${s}")
    if(NOT "${version}" STREQUAL "")
      list(APPEND version_list ${version})
    else()
      message(FATAL_ERROR "Parse error could not find version in [${s}]")
    endif()
  endforeach()
  if(NOT DEFINED has_match)
    message("Information: no embedded manifest in: ${file}")
    return()
  endif()
  list(APPEND version_list ${${list_var}})
  list(REMOVE_DUPLICATES version_list)
  if(version_list)
    set(${list_var} ${version_list} PARENT_SCOPE)
  endif()
endfunction()
set(fatal_error FALSE)

# check_version:
#
# test a file against the shipped manifest versions
# for a directory
function(check_version file manifest_versions)
  set(manifest_versions ${manifest_versions} ${allow_versions})
  # collect versions for a given file
  crt_version(${file} file_versions)
  # see if the versions
  foreach(ver ${file_versions})
    list(FIND manifest_versions "${ver}" found_version)
    if("${found_version}" EQUAL -1)
      message("ERROR: ${file} uses ${ver} not found in shipped manifests:[${manifest_versions}].")
      set(fatal_error TRUE PARENT_SCOPE)
    endif()
  endforeach()
  list(LENGTH file_versions len)
  if(${len} GREATER 1)
    message("WARNING: found more than one version of MICROSOFT.VC80.CRT referenced in ${file}: [${file_versions}]")
  endif()
endfunction()

# collect up the versions of CRT that are shipped
# in .manifest files
set(manifest_version_list )
file(GLOB_RECURSE manifest_files "*.manifest")
foreach(f ${manifest_files})
  crt_version("${f}" manifest_version_list)
endforeach()
list(LENGTH manifest_version_list LEN)
if(LEN EQUAL 0)
  message(FATAL_ERROR "No .manifest files found, no version check can be done.")
endif()
message("Versions found in ${manifest_files}: ${manifest_version_list}")
if(DEFINED allow_versions)
  message("Extra versions allowed: ${allow_versions}")
endif()

# now find all .exe and .dll files
# and call check_version on each of them
file(GLOB_RECURSE exe_files "*.exe")
file(GLOB_RECURSE dll_files "*.dll")
set(exe_files ${exe_files} ${dll_files})
foreach(f ${exe_files})
  check_version(${f} "${manifest_version_list}")
endforeach()

# report a fatal error if there were any so that cmake will return
# a non zero value
if(fatal_error)
  message(FATAL_ERROR "This distribution embeds dll "
    " versions that it does not ship, and may not work on other machines.")
endif()
