Visual Studio 12 2013
---------------------

Generates Visual Studio 12 (VS 2013) project files.

For compatibility with CMake versions prior to 3.0, one may specify this
generator using the name "Visual Studio 12" without the year component.

Project Types
^^^^^^^^^^^^^

Only Visual C++ and C# projects may be generated.  Other types of
projects (JavaScript, Powershell, Python, etc.) are not supported.

Platform Selection
^^^^^^^^^^^^^^^^^^

The default target platform name (architecture) is ``Win32``.

The :variable:`CMAKE_GENERATOR_PLATFORM` variable may be set, perhaps
via the :manual:`cmake(1)` ``-A`` option, to specify a target platform
name (architecture).  For example:

* ``cmake -G "Visual Studio 12 2013" -A Win32``
* ``cmake -G "Visual Studio 12 2013" -A x64``
* ``cmake -G "Visual Studio 12 2013" -A ARM``

For compatibility with CMake versions prior to 3.1, one may specify
a target platform name optionally at the end of the generator name.
This is supported only for:

``Visual Studio 12 2013 Win64``
  Specify target platform ``x64``.

``Visual Studio 12 2013 ARM``
  Specify target platform ``ARM``.

Toolset Selection
^^^^^^^^^^^^^^^^^

The ``v120`` toolset that comes with Visual Studio 12 2013 is selected by
default.  The :variable:`CMAKE_GENERATOR_TOOLSET` option may be set, perhaps
via the :manual:`cmake(1)` ``-T`` option, to specify another toolset.

.. |VS_TOOLSET_HOST_ARCH_DEFAULT| replace::
   By default this generator uses the 32-bit variant even on a 64-bit host.

.. include:: VS_TOOLSET_HOST_ARCH.txt
