IMPORTED_OBJECTS
----------------

.. versionadded:: 3.9

A :ref:`semicolon-separated list <CMake Language Lists>` of absolute paths to the object
files on disk for an :ref:`imported <Imported targets>`
:ref:`object library <object libraries>`.

Ignored for non-imported targets.

Projects may skip ``IMPORTED_OBJECTS`` if the configuration-specific
property :prop_tgt:`IMPORTED_OBJECTS_<CONFIG>` is set instead.
