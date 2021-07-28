# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CMakeFindDependencyMacro
-------------------------

.. command:: find_dependency

  The ``find_dependency()`` macro wraps a :command:`find_package` call for
  a package dependency::

    find_dependency(<dep> [...])

  It is designed to be used in a
  :ref:`Package Configuration File <Config File Packages>`
  (``<PackageName>Config.cmake``).  ``find_dependency`` forwards the correct
  parameters for ``QUIET`` and ``REQUIRED`` which were passed to
  the original :command:`find_package` call.  Any additional arguments
  specified are forwarded to :command:`find_package`.

  If the dependency could not be found it sets an informative diagnostic
  message and calls :command:`return` to end processing of the calling
  package configuration file and return to the :command:`find_package`
  command that loaded it.

  .. note::

    The call to :command:`return` makes this macro unsuitable to call
    from :ref:`Find Modules`.
#]=======================================================================]

macro(find_dependency dep)
  set(cmake_fd_quiet_arg)
  if(${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
    set(cmake_fd_quiet_arg QUIET)
  endif()
  set(cmake_fd_required_arg)
  if(${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED)
    set(cmake_fd_required_arg REQUIRED)
  endif()

  get_property(cmake_fd_alreadyTransitive GLOBAL PROPERTY
    _CMAKE_${dep}_TRANSITIVE_DEPENDENCY
  )

  find_package(${dep} ${ARGN}
    ${cmake_fd_quiet_arg}
    ${cmake_fd_required_arg}
  )

  if(NOT DEFINED cmake_fd_alreadyTransitive OR cmake_fd_alreadyTransitive)
    set_property(GLOBAL PROPERTY _CMAKE_${dep}_TRANSITIVE_DEPENDENCY TRUE)
  endif()

  if (NOT ${dep}_FOUND)
    set(${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE "${CMAKE_FIND_PACKAGE_NAME} could not be found because dependency ${dep} could not be found.")
    set(${CMAKE_FIND_PACKAGE_NAME}_FOUND False)
    return()
  endif()
  set(cmake_fd_required_arg)
  set(cmake_fd_quiet_arg)
endmacro()
