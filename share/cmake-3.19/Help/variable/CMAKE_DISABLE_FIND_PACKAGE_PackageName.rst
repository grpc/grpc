CMAKE_DISABLE_FIND_PACKAGE_<PackageName>
----------------------------------------

Variable for disabling :command:`find_package` calls.

Every non-``REQUIRED`` :command:`find_package` call in a project can be
disabled by setting the variable
``CMAKE_DISABLE_FIND_PACKAGE_<PackageName>`` to ``TRUE``.
This can be used to build a project without an optional package,
although that package is installed.

This switch should be used during the initial CMake run.  Otherwise if
the package has already been found in a previous CMake run, the
variables which have been stored in the cache will still be there.  In
that case it is recommended to remove the cache variables for this
package from the cache using the cache editor or :manual:`cmake(1)` ``-U``
