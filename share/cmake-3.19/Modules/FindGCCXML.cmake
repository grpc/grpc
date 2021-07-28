# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindGCCXML
----------

Find the GCC-XML front-end executable.



This module will define the following variables:

::

  GCCXML - the GCC-XML front-end executable.
#]=======================================================================]

find_program(GCCXML
  NAMES gccxml
        ../GCC_XML/gccxml
  PATHS [HKEY_CURRENT_USER\\Software\\Kitware\\GCC_XML;loc]
  "$ENV{ProgramFiles}/GCC_XML"
  "C:/Program Files/GCC_XML"
)

mark_as_advanced(GCCXML)
