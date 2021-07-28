CMAKE_ANDROID_API
-----------------

.. versionadded:: 3.1

When :ref:`Cross Compiling for Android with NVIDIA Nsight Tegra Visual Studio
Edition`, this variable may be set to specify the default value for the
:prop_tgt:`ANDROID_API` target property.  See that target property for
additional information.

Otherwise, when :ref:`Cross Compiling for Android`, this variable provides
the Android API version number targeted.  This will be the same value as
the :variable:`CMAKE_SYSTEM_VERSION` variable for ``Android`` platforms.
