CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION
----------------------------------------

.. versionadded:: 3.4

Visual Studio Windows Target Platform Version.

When targeting Windows 10 and above Visual Studio 2015 and above support
specification of a target Windows version to select a corresponding SDK.
The :variable:`CMAKE_SYSTEM_VERSION` variable may be set to specify a
version.  Otherwise CMake computes a default version based on the Windows
SDK versions available.  The chosen Windows target version number is provided
in ``CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION``.  If no Windows 10 SDK
is available this value will be empty.

One may set a ``CMAKE_WINDOWS_KITS_10_DIR`` *environment variable*
to an absolute path to tell CMake to look for Windows 10 SDKs in
a custom location.  The specified directory is expected to contain
``Include/10.0.*`` directories.

See also :variable:`CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION_MAXIMUM`.
