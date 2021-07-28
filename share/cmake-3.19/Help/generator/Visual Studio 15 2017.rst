Visual Studio 15 2017
---------------------

.. versionadded:: 3.7.1

Generates Visual Studio 15 (VS 2017) project files.

Project Types
^^^^^^^^^^^^^

Only Visual C++ and C# projects may be generated.  Other types of
projects (JavaScript, Powershell, Python, etc.) are not supported.

Instance Selection
^^^^^^^^^^^^^^^^^^

VS 2017 supports multiple installations on the same machine.
The :variable:`CMAKE_GENERATOR_INSTANCE` variable may be set as a
cache entry containing the absolute path to a Visual Studio instance.
If the value is not specified explicitly by the user or a toolchain file,
CMake queries the Visual Studio Installer to locate VS instances, chooses
one, and sets the variable as a cache entry to hold the value persistently.

When CMake first chooses an instance, if the ``VS150COMNTOOLS`` environment
variable is set and points to the ``Common7/Tools`` directory within
one of the instances, that instance will be used.  Otherwise, if more
than one instance is installed we do not define which one is chosen
by default.

Platform Selection
^^^^^^^^^^^^^^^^^^

The default target platform name (architecture) is ``Win32``.

The :variable:`CMAKE_GENERATOR_PLATFORM` variable may be set, perhaps
via the :manual:`cmake(1)` ``-A`` option, to specify a target platform
name (architecture).  For example:

* ``cmake -G "Visual Studio 15 2017" -A Win32``
* ``cmake -G "Visual Studio 15 2017" -A x64``
* ``cmake -G "Visual Studio 15 2017" -A ARM``
* ``cmake -G "Visual Studio 15 2017" -A ARM64``

For compatibility with CMake versions prior to 3.1, one may specify
a target platform name optionally at the end of the generator name.
This is supported only for:

``Visual Studio 15 2017 Win64``
  Specify target platform ``x64``.

``Visual Studio 15 2017 ARM``
  Specify target platform ``ARM``.

Toolset Selection
^^^^^^^^^^^^^^^^^

The ``v141`` toolset that comes with Visual Studio 15 2017 is selected by
default.  The :variable:`CMAKE_GENERATOR_TOOLSET` option may be set, perhaps
via the :manual:`cmake(1)` ``-T`` option, to specify another toolset.

.. |VS_TOOLSET_HOST_ARCH_DEFAULT| replace::
   By default this generator uses the 32-bit variant even on a 64-bit host.

.. include:: VS_TOOLSET_HOST_ARCH.txt
