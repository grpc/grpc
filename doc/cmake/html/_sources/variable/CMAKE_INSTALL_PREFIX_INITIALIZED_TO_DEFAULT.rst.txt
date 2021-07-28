CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT
-------------------------------------------

.. versionadded:: 3.7.1

CMake sets this variable to a ``TRUE`` value when the
:variable:`CMAKE_INSTALL_PREFIX` has just been initialized to
its default value, typically on the first run of CMake within
a new build tree.  This can be used by project code to change
the default without overriding a user-provided value:

.. code-block:: cmake

  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/my/default" CACHE PATH "..." FORCE)
  endif()
