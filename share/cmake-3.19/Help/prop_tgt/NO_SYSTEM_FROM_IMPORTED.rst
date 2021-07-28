NO_SYSTEM_FROM_IMPORTED
-----------------------

Do not treat include directories from the interfaces of consumed
:ref:`imported targets` as ``SYSTEM``.

The contents of the :prop_tgt:`INTERFACE_INCLUDE_DIRECTORIES` target property
of imported targets are treated as ``SYSTEM`` includes by default.  If this
property is enabled on a target, compilation of sources in that target will
not treat the contents of the ``INTERFACE_INCLUDE_DIRECTORIES`` of consumed
imported targets as system includes.

This property is initialized by the value of the
:variable:`CMAKE_NO_SYSTEM_FROM_IMPORTED` variable if it is set when a target
is created.
