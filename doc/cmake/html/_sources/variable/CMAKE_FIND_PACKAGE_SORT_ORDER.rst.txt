CMAKE_FIND_PACKAGE_SORT_ORDER
-----------------------------

.. versionadded:: 3.7

The default order for sorting packages found using :command:`find_package`.
It can assume one of the following values:

``NONE``
  Default.  No attempt is done to sort packages.
  The first valid package found will be selected.

``NAME``
  Sort packages lexicographically before selecting one.

``NATURAL``
  Sort packages using natural order (see ``strverscmp(3)`` manual),
  i.e. such that contiguous digits are compared as whole numbers.

Natural sorting can be employed to return the highest version when multiple
versions of the same library are found by :command:`find_package`.  For
example suppose that the following libraries have been found:

* libX-1.1.0
* libX-1.2.9
* libX-1.2.10

By setting ``NATURAL`` order we can select the one with the highest
version number ``libX-1.2.10``.

.. code-block:: cmake

  set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
  find_package(libX CONFIG)

The sort direction can be controlled using the
:variable:`CMAKE_FIND_PACKAGE_SORT_DIRECTION` variable
(by default decrescent, e.g. lib-B will be tested before lib-A).
