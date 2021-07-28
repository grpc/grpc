CMAKE_OSX_DEPLOYMENT_TARGET
---------------------------

Specify the minimum version of the target platform (e.g. macOS or iOS)
on which the target binaries are to be deployed.  CMake uses this
variable value for the ``-mmacosx-version-min`` flag or their respective
target platform equivalents.  For older Xcode versions that shipped
multiple macOS SDKs this variable also helps to choose the SDK in case
:variable:`CMAKE_OSX_SYSROOT` is unset.

If not set explicitly the value is initialized by the
``MACOSX_DEPLOYMENT_TARGET`` environment variable, if set,
and otherwise computed based on the host platform.

.. include:: CMAKE_OSX_VARIABLE.txt
