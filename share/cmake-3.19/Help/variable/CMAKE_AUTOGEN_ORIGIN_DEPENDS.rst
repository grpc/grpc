CMAKE_AUTOGEN_ORIGIN_DEPENDS
----------------------------

.. versionadded:: 3.14

Switch for forwarding origin target dependencies to the corresponding
``_autogen`` targets.

This variable is used to initialize the :prop_tgt:`AUTOGEN_ORIGIN_DEPENDS`
property on all the targets.  See that target property for additional
information.

By default :variable:`CMAKE_AUTOGEN_ORIGIN_DEPENDS` is ``ON``.
