CMAKE_FIND_PACKAGE_SORT_DIRECTION
---------------------------------

.. versionadded:: 3.7

The sorting direction used by :variable:`CMAKE_FIND_PACKAGE_SORT_ORDER`.
It can assume one of the following values:

``DEC``
  Default.  Ordering is done in descending mode.
  The highest folder found will be tested first.

``ASC``
  Ordering is done in ascending mode.
  The lowest folder found will be tested first.

If :variable:`CMAKE_FIND_PACKAGE_SORT_ORDER` is not set or is set to ``NONE``
this variable has no effect.
