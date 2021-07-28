subdir_depends
--------------

Disallowed since version 3.0.  See CMake Policy :policy:`CMP0029`.

Does nothing.

::

  subdir_depends(subdir dep1 dep2 ...)

Does not do anything.  This command used to help projects order
parallel builds correctly.  This functionality is now automatic.
