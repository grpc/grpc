CMAKE_OSX_SYSROOT
-----------------

Specify the location or name of the macOS platform SDK to be used.
CMake uses this value to compute the value of the ``-isysroot`` flag
or equivalent and to help the ``find_*`` commands locate files in
the SDK.

If not set explicitly the value is initialized by the ``SDKROOT``
environment variable, if set, and otherwise computed based on the
:variable:`CMAKE_OSX_DEPLOYMENT_TARGET` or the host platform.

.. include:: CMAKE_OSX_VARIABLE.txt
