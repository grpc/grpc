IMPORTED_IMPLIB
---------------

Full path to the import library for an ``IMPORTED`` target.

Set this to the location of the ``.lib`` part of a Windows DLL, or on
AIX set it to an import file created for executables that export symbols
(see the :prop_tgt:`ENABLE_EXPORTS` target property).
Ignored for non-imported targets.
