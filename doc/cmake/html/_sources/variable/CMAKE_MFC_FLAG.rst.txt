CMAKE_MFC_FLAG
--------------

Use the MFC library for an executable or dll.

Enables the use of the Microsoft Foundation Classes (MFC).
It should be set to ``1`` for the static MFC library, and
``2`` for the shared MFC library.  This is used in Visual Studio
project files.

Usage example:

.. code-block:: cmake

  add_definitions(-D_AFXDLL)
  set(CMAKE_MFC_FLAG 2)
  add_executable(CMakeSetup WIN32 ${SRCS})

Contents of ``CMAKE_MFC_FLAG`` may use
:manual:`generator expressions <cmake-generator-expressions(7)>`.
