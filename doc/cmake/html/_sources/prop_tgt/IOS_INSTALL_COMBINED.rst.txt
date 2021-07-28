IOS_INSTALL_COMBINED
--------------------

.. versionadded:: 3.5

Build a combined (device and simulator) target when installing.

When this property is set to set to false (which is the default) then it will
either be built with the device SDK or the simulator SDK depending on the SDK
set. But if this property is set to true then the target will at install time
also be built for the corresponding SDK and combined into one library.

.. note::

  If a selected architecture is available for both: device SDK and simulator
  SDK it will be built for the SDK selected by :variable:`CMAKE_OSX_SYSROOT`
  and removed from the corresponding SDK.

This feature requires at least Xcode version 6.
