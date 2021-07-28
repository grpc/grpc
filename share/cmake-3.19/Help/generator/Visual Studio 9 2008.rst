Visual Studio 9 2008
--------------------

Generates Visual Studio 9 2008 project files.

Platform Selection
^^^^^^^^^^^^^^^^^^

The default target platform name (architecture) is ``Win32``.

The :variable:`CMAKE_GENERATOR_PLATFORM` variable may be set, perhaps
via the :manual:`cmake(1)` ``-A`` option, to specify a target platform
name (architecture).  For example:

* ``cmake -G "Visual Studio 9 2008" -A Win32``
* ``cmake -G "Visual Studio 9 2008" -A x64``
* ``cmake -G "Visual Studio 9 2008" -A Itanium``
* ``cmake -G "Visual Studio 9 2008" -A <WinCE-SDK>``
  (Specify a target platform matching a Windows CE SDK name.)

For compatibility with CMake versions prior to 3.1, one may specify
a target platform name optionally at the end of the generator name.
This is supported only for:

``Visual Studio 9 2008 Win64``
  Specify target platform ``x64``.

``Visual Studio 9 2008 IA64``
  Specify target platform ``Itanium``.

``Visual Studio 9 2008 <WinCE-SDK>``
  Specify target platform matching a Windows CE SDK name.
