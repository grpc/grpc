Visual Studio 16 2019
---------------------

.. versionadded:: 3.14

Generates Visual Studio 16 (VS 2019) project files.

Project Types
^^^^^^^^^^^^^

Only Visual C++ and C# projects may be generated.  Other types of
projects (JavaScript, Powershell, Python, etc.) are not supported.

Instance Selection
^^^^^^^^^^^^^^^^^^

VS 2019 supports multiple installations on the same machine.
The :variable:`CMAKE_GENERATOR_INSTANCE` variable may be set as a
cache entry containing the absolute path to a Visual Studio instance.
If the value is not specified explicitly by the user or a toolchain file,
CMake queries the Visual Studio Installer to locate VS instances, chooses
one, and sets the variable as a cache entry to hold the value persistently.

When CMake first chooses an instance, if the ``VS160COMNTOOLS`` environment
variable is set and points to the ``Common7/Tools`` directory within
one of the instances, that instance will be used.  Otherwise, if more
than one instance is installed we do not define which one is chosen
by default.

Platform Selection
^^^^^^^^^^^^^^^^^^

The default target platform name (architecture) is that of the host
and is provided in the :variable:`CMAKE_VS_PLATFORM_NAME_DEFAULT` variable.

The :variable:`CMAKE_GENERATOR_PLATFORM` variable may be set, perhaps
via the :manual:`cmake(1)` ``-A`` option, to specify a target platform
name (architecture).  For example:

* ``cmake -G "Visual Studio 16 2019" -A Win32``
* ``cmake -G "Visual Studio 16 2019" -A x64``
* ``cmake -G "Visual Studio 16 2019" -A ARM``
* ``cmake -G "Visual Studio 16 2019" -A ARM64``

Toolset Selection
^^^^^^^^^^^^^^^^^

The ``v142`` toolset that comes with Visual Studio 16 2019 is selected by
default.  The :variable:`CMAKE_GENERATOR_TOOLSET` option may be set, perhaps
via the :manual:`cmake(1)` ``-T`` option, to specify another toolset.

.. |VS_TOOLSET_HOST_ARCH_DEFAULT| replace::
   By default this generator uses the 64-bit variant on x64 hosts and
   the 32-bit variant otherwise.

.. include:: VS_TOOLSET_HOST_ARCH.txt
