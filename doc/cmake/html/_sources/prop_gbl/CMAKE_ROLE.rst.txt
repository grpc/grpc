CMAKE_ROLE
----------

.. versionadded:: 3.14

Tells what mode the current running script is in. Could be one of several
values:

``PROJECT``
  Running in project mode (processing a ``CMakeLists.txt`` file).

``SCRIPT``
  Running in ``-P`` script mode.

``FIND_PACKAGE``
  Running in ``--find-package`` mode.

``CTEST``
  Running in CTest script mode.

``CPACK``
  Running in CPack.
