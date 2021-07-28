Visual Studio 11 2012
---------------------

Generates Visual Studio 11 (VS 2012) project files.

For compatibility with CMake versions prior to 3.0, one may specify this
generator using the name "Visual Studio 11" without the year component.

Project Types
^^^^^^^^^^^^^

Only Visual C++ and C# projects may be generated.  Other types of
projects (JavaScript, Database, Website, etc.) are not supported.

Platform Selection
^^^^^^^^^^^^^^^^^^

The default target platform name (architecture) is ``Win32``.

The :variable:`CMAKE_GENERATOR_PLATFORM` variable may be set, perhaps
via the :manual:`cmake(1)` ``-A`` option, to specify a target platform
name (architecture).  For example:

* ``cmake -G "Visual Studio 11 2012" -A Win32``
* ``cmake -G "Visual Studio 11 2012" -A x64``
* ``cmake -G "Visual Studio 11 2012" -A ARM``
* ``cmake -G "Visual Studio 11 2012" -A <WinCE-SDK>``
  (Specify a target platform matching a Windows CE SDK name.)

For compatibility with CMake versions prior to 3.1, one may specify
a target platform name optionally at the end of the generator name.
This is supported only for:

``Visual Studio 11 2012 Win64``
  Specify target platform ``x64``.

``Visual Studio 11 2012 ARM``
  Specify target platform ``ARM``.

``Visual Studio 11 2012 <WinCE-SDK>``
  Specify target platform matching a Windows CE SDK name.

Toolset Selection
^^^^^^^^^^^^^^^^^

The ``v110`` toolset that comes with Visual Studio 11 2012 is selected by
default.  The :variable:`CMAKE_GENERATOR_TOOLSET` option may be set, perhaps
via the :manual:`cmake(1)` ``-T`` option, to specify another toolset.
