# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CMakeDependentOption
--------------------

Macro to provide an option dependent on other options.

This macro presents an option to the user only if a set of other
conditions are true.

Usage:

.. code-block:: cmake

  cmake_dependent_option(<option> "<help_text>" <value> <depends> <force>)

Where ``<option>`` is available to the user if ``<depends>`` is true. When
``<option>`` is available, the given ``<help_text>`` and initial ``<value>``
are used. If the ``<depends>`` condition is not true, ``<option>`` will not be
presented and will always have the value given by ``<force>``. Any value set by
the user is preserved for when the option is presented again. Each element in
the fourth parameter is evaluated as an if-condition, so
:ref:`Condition Syntax` can be used.

Example invocation:

.. code-block:: cmake

  cmake_dependent_option(USE_FOO "Use Foo" ON
                         "USE_BAR;NOT USE_ZOT" OFF)

If ``USE_BAR`` is true and ``USE_ZOT`` is false, this provides an option called
``USE_FOO`` that defaults to ON. Otherwise, it sets ``USE_FOO`` to OFF and
hides the option from the user. If the status of ``USE_BAR`` or ``USE_ZOT``
ever changes, any value for the ``USE_FOO`` option is saved so that when the
option is re-enabled it retains its old value.
#]=======================================================================]

macro(CMAKE_DEPENDENT_OPTION option doc default depends force)
  if(${option}_ISSET MATCHES "^${option}_ISSET$")
    set(${option}_AVAILABLE 1)
    foreach(d ${depends})
      string(REGEX REPLACE " +" ";" CMAKE_DEPENDENT_OPTION_DEP "${d}")
      if(${CMAKE_DEPENDENT_OPTION_DEP})
      else()
        set(${option}_AVAILABLE 0)
      endif()
    endforeach()
    if(${option}_AVAILABLE)
      option(${option} "${doc}" "${default}")
      set(${option} "${${option}}" CACHE BOOL "${doc}" FORCE)
    else()
      if(${option} MATCHES "^${option}$")
      else()
        set(${option} "${${option}}" CACHE INTERNAL "${doc}")
      endif()
      set(${option} ${force})
    endif()
  else()
    set(${option} "${${option}_ISSET}")
  endif()
endmacro()
