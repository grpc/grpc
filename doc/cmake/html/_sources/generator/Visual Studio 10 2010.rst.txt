Visual Studio 10 2010
---------------------

Generates Visual Studio 10 (VS 2010) project files.

For compatibility with CMake versions prior to 3.0, one may specify this
generator using the name ``Visual Studio 10`` without the year component.

Project Types
^^^^^^^^^^^^^

Only Visual C++ and C# projects may be generated.  Other types of
projects (Database, Website, etc.) are not supported.

Platform Selection
^^^^^^^^^^^^^^^^^^

The default target platform name (architecture) is ``Win32``.

The :variable:`CMAKE_GENERATOR_PLATFORM` variable may be set, perhaps
via the :manual:`cmake(1)` ``-A`` option, to specify a target platform
name (architecture).  For example:

* ``cmake -G "Visual Studio 10 2010" -A Win32``
* ``cmake -G "Visual Studio 10 2010" -A x64``
* ``cmake -G "Visual Studio 10 2010" -A Itanium``

For compatibility with CMake versions prior to 3.1, one may specify
a target platform name optionally at the end of the generator name.
This is supported only for:

``Visual Studio 10 2010 Win64``
  Specify target platform ``x64``.

``Visual Studio 10 2010 IA64``
  Specify target platform ``Itanium``.

Toolset Selection
^^^^^^^^^^^^^^^^^

The ``v100`` toolset that comes with Visual Studio 10 2010 is selected by
default.  The :variable:`CMAKE_GENERATOR_TOOLSET` option may be set, perhaps
via the :manual:`cmake(1)` ``-T`` option, to specify another toolset.
