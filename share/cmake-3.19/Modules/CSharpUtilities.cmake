# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CSharpUtilities
---------------

.. versionadded:: 3.8

Functions to make configuration of CSharp/.NET targets easier.

A collection of CMake utility functions useful for dealing with CSharp
targets for Visual Studio generators from version 2010 and later.

The following functions are provided by this module:

**Main functions**

- :command:`csharp_set_windows_forms_properties`
- :command:`csharp_set_designer_cs_properties`
- :command:`csharp_set_xaml_cs_properties`

**Helper functions**

- :command:`csharp_get_filename_keys`
- :command:`csharp_get_filename_key_base`
- :command:`csharp_get_dependentupon_name`

Main functions provided by the module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. command:: csharp_set_windows_forms_properties

  Sets source file properties for use of Windows Forms. Use this, if your CSharp
  target uses Windows Forms::

    csharp_set_windows_forms_properties([<file1> [<file2> [...]]])

  ``<fileN>``
    List of all source files which are relevant for setting the
    :prop_sf:`VS_CSHARP_<tagname>` properties (including ``.cs``, ``.resx`` and
    ``.Designer.cs`` extensions).

  In the list of all given files for all files ending with ``.Designer.cs`` and
  ``.resx`` is searched.  For every *designer* or *resource* file a file with the
  same base name but only ``.cs`` as extension is searched.  If this is found, the
  :prop_sf:`VS_CSHARP_<tagname>` properties are set as follows:

  for the **.cs** file:
   - VS_CSHARP_SubType "Form"

  for the **.Designer.cs** file (if it exists):
   - VS_CSHARP_DependentUpon <cs-filename>
   - VS_CSHARP_DesignTime "" (delete tag if previously defined)
   - VS_CSHARP_AutoGen ""(delete tag if previously defined)

  for the **.resx** file (if it exists):
   - VS_RESOURCE_GENERATOR "" (delete tag if previously defined)
   - VS_CSHARP_DependentUpon <cs-filename>
   - VS_CSHARP_SubType "Designer"

.. command:: csharp_set_designer_cs_properties

  Sets source file properties of ``.Designer.cs`` files depending on
  sibling filenames. Use this, if your CSharp target does **not**
  use Windows Forms (for Windows Forms use
  :command:`csharp_set_designer_cs_properties` instead)::

    csharp_set_designer_cs_properties([<file1> [<file2> [...]]])

  ``<fileN>``
    List of all source files which are relevant for setting the
    :prop_sf:`VS_CSHARP_<tagname>` properties (including ``.cs``,
    ``.resx``, ``.settings`` and ``.Designer.cs`` extensions).

  In the list of all given files for all files ending with
  ``.Designer.cs`` is searched. For every *designer* file all files
  with the same base name but different extensions are searched. If
  a match is found, the source file properties of the *designer* file
  are set depending on the extension of the matched file:

  if match is **.resx** file:
   - VS_CSHARP_AutoGen "True"
   - VS_CSHARP_DesignTime "True"
   - VS_CSHARP_DependentUpon <resx-filename>

  if match is **.cs** file:
   - VS_CSHARP_DependentUpon <cs-filename>

  if match is **.settings** file:
   - VS_CSHARP_AutoGen "True"
   - VS_CSHARP_DesignTimeSharedInput "True"
   - VS_CSHARP_DependentUpon <settings-filename>

.. note::

    Because the source file properties of the ``.Designer.cs`` file are set according
    to the found matches and every match sets the **VS_CSHARP_DependentUpon**
    property, there should only be one match for each ``Designer.cs`` file.

.. command:: csharp_set_xaml_cs_properties

  Sets source file properties for use of Windows Presentation Foundation (WPF) and
  XAML. Use this, if your CSharp target uses WPF/XAML::

    csharp_set_xaml_cs_properties([<file1> [<file2> [...]]])

  ``<fileN>``
    List of all source files which are relevant for setting the
    :prop_sf:`VS_CSHARP_<tagname>` properties (including ``.cs``,
    ``.xaml``, and ``.xaml.cs`` extensions).

  In the list of all given files for all files ending with
  ``.xaml.cs`` is searched. For every *xaml-cs* file, a file
  with the same base name but extension ``.xaml`` is searched.
  If a match is found, the source file properties of the ``.xaml.cs``
  file are set:

   - VS_CSHARP_DependentUpon <xaml-filename>

Helper functions which are used by the above ones
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. command:: csharp_get_filename_keys

  Helper function which computes a list of key values to identify
  source files independently of relative/absolute paths given in cmake
  and eliminates case sensitivity::

    csharp_get_filename_keys(OUT [<file1> [<file2> [...]]])

  ``OUT``
    Name of the variable in which the list of keys is stored

  ``<fileN>``
    filename(s) as given to to CSharp target using :command:`add_library`
    or :command:`add_executable`

  In some way the function applies a canonicalization to the source names.
  This is necessary to find file matches if the files have been added to
  the target with different directory prefixes:

  .. code-block:: cmake

    add_library(lib
      myfile.cs
      ${CMAKE_CURRENT_SOURCE_DIR}/myfile.Designer.cs)

    set_source_files_properties(myfile.Designer.cs PROPERTIES
      VS_CSHARP_DependentUpon myfile.cs)

    # this will fail, because in cmake
    #  - ${CMAKE_CURRENT_SOURCE_DIR}/myfile.Designer.cs
    #  - myfile.Designer.cs
    # are not the same source file. The source file property is not set.

.. command:: csharp_get_filename_key_base

  Returns the full filepath and name **without** extension of a key.
  KEY is expected to be a key from csharp_get_filename_keys. In BASE
  the value of KEY without the file extension is returned::

    csharp_get_filename_key_base(BASE KEY)

  ``BASE``
    Name of the variable with the computed "base" of ``KEY``.

  ``KEY``
    The key of which the base will be computed. Expected to be a
    upper case full filename.

.. command:: csharp_get_dependentupon_name

  Computes a string which can be used as value for the source file property
  :prop_sf:`VS_CSHARP_<tagname>` with *target* being ``DependentUpon``::

    csharp_get_dependentupon_name(NAME FILE)

  ``NAME``
    Name of the variable with the result value

  ``FILE``
    Filename to convert to ``<DependentUpon>`` value

  Actually this is only the filename without any path given at the moment.

#]=======================================================================]

cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW) # if IN_LIST

function(csharp_get_filename_keys OUT)
  set(${OUT} "")
  foreach(f ${ARGN})
    get_filename_component(f ${f} REALPATH)
    string(TOUPPER ${f} f)
    list(APPEND ${OUT} ${f})
  endforeach()
  set(${OUT} "${${OUT}}" PARENT_SCOPE)
endfunction()

function(csharp_get_filename_key_base base key)
  get_filename_component(dir ${key} DIRECTORY)
  get_filename_component(fil ${key} NAME_WE)
  set(${base} "${dir}/${fil}" PARENT_SCOPE)
endfunction()

function(csharp_get_dependentupon_name out in)
  get_filename_component(${out} ${in} NAME)
  set(${out} ${${out}} PARENT_SCOPE)
endfunction()

function(csharp_set_windows_forms_properties)
  csharp_get_filename_keys(fileKeys ${ARGN})
  foreach(key ${fileKeys})
    get_filename_component(ext ${key} EXT)
    if(${ext} STREQUAL ".DESIGNER.CS" OR
       ${ext} STREQUAL ".RESX")
      csharp_get_filename_key_base(NAME_BASE ${key})
      list(FIND fileKeys "${NAME_BASE}.CS" FILE_INDEX)
      if(NOT ${FILE_INDEX} EQUAL -1)
        list(GET ARGN ${FILE_INDEX} FILE_NAME)
        # set properties of main form file
        set_source_files_properties("${FILE_NAME}"
          PROPERTIES
          VS_CSHARP_SubType "Form")
        csharp_get_dependentupon_name(LINK "${FILE_NAME}")
        # set properties of designer file (if found)
        list(FIND fileKeys "${NAME_BASE}.DESIGNER.CS" FILE_INDEX)
        if(NOT ${FILE_INDEX} EQUAL -1)
          list(GET ARGN ${FILE_INDEX} FILE_NAME)
          set_source_files_properties("${FILE_NAME}"
            PROPERTIES
            VS_CSHARP_DependentUpon "${LINK}"
            VS_CSHARP_DesignTime ""
            VS_CSHARP_AutoGen "")
        endif()
        # set properties of corresponding resource file (if found)
        list(FIND fileKeys "${NAME_BASE}.RESX" FILE_INDEX)
        if(NOT ${FILE_INDEX} EQUAL -1)
          list(GET ARGN ${FILE_INDEX} FILE_NAME)
          set_source_files_properties("${FILE_NAME}"
            PROPERTIES
            VS_RESOURCE_GENERATOR ""
            VS_CSHARP_DependentUpon "${LINK}"
            VS_CSHARP_SubType "Designer")
        endif()
      endif()
    endif()
  endforeach()
endfunction()

function(csharp_set_designer_cs_properties)
  csharp_get_filename_keys(fileKeys ${ARGN})
  set(INDEX -1)
  foreach(key ${fileKeys})
    math(EXPR INDEX "${INDEX}+1")
    list(GET ARGN ${INDEX} source)
    get_filename_component(ext ${key} EXT)
    if(${ext} STREQUAL ".DESIGNER.CS")
      csharp_get_filename_key_base(NAME_BASE ${key})
      if("${NAME_BASE}.RESX" IN_LIST fileKeys)
        list(FIND fileKeys "${NAME_BASE}.RESX" FILE_INDEX)
        list(GET ARGN ${FILE_INDEX} FILE_NAME)
        csharp_get_dependentupon_name(LINK "${FILE_NAME}")
        set_source_files_properties("${source}"
          PROPERTIES
          VS_CSHARP_AutoGen "True"
          VS_CSHARP_DesignTime "True"
          VS_CSHARP_DependentUpon "${LINK}")
      elseif("${NAME_BASE}.CS" IN_LIST fileKeys)
        list(FIND fileKeys "${NAME_BASE}.CS" FILE_INDEX)
        list(GET ARGN ${FILE_INDEX} FILE_NAME)
        csharp_get_dependentupon_name(LINK "${FILE_NAME}")
        set_source_files_properties("${source}"
          PROPERTIES
          VS_CSHARP_DependentUpon "${LINK}")
      elseif("${NAME_BASE}.SETTINGS" IN_LIST fileKeys)
        list(FIND fileKeys "${NAME_BASE}.SETTINGS" FILE_INDEX)
        list(GET ARGN ${FILE_INDEX} FILE_NAME)
        csharp_get_dependentupon_name(LINK "${FILE_NAME}")
        set_source_files_properties("${source}"
          PROPERTIES
          VS_CSHARP_AutoGen "True"
          VS_CSHARP_DesignTimeSharedInput "True"
          VS_CSHARP_DependentUpon "${LINK}")
      endif()
    endif()
  endforeach()
endfunction()

function(csharp_set_xaml_cs_properties)
  csharp_get_filename_keys(fileKeys ${ARGN})
  set(INDEX -1)
  foreach(key ${fileKeys})
    math(EXPR INDEX "${INDEX}+1")
    list(GET ARGN ${INDEX} source)
    get_filename_component(ext ${key} EXT)
    if(${ext} STREQUAL ".XAML.CS")
      csharp_get_filename_key_base(NAME_BASE ${key})
      if("${NAME_BASE}.XAML" IN_LIST fileKeys)
        list(FIND fileKeys "${NAME_BASE}.XAML" FILE_INDEX)
        list(GET ARGN ${FILE_INDEX} FILE_NAME)
        csharp_get_dependentupon_name(LINK "${FILE_NAME}")
        set_source_files_properties("${source}"
          PROPERTIES
          VS_CSHARP_DependentUpon "${LINK}")
      endif()
    endif()
  endforeach()
endfunction()

cmake_policy(POP)
