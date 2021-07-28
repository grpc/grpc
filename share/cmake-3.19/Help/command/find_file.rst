find_file
---------

.. |FIND_XXX| replace:: find_file
.. |NAMES| replace:: NAMES name1 [name2 ...]
.. |SEARCH_XXX| replace:: full path to a file
.. |SEARCH_XXX_DESC| replace:: full path to named file
.. |prefix_XXX_SUBDIR| replace:: ``<prefix>/include``
.. |entry_XXX_SUBDIR| replace:: ``<entry>/include``

.. |FIND_PACKAGE_ROOT_PREFIX_PATH_XXX| replace::
   ``<prefix>/include/<arch>`` if :variable:`CMAKE_LIBRARY_ARCHITECTURE`
   is set, and |FIND_PACKAGE_ROOT_PREFIX_PATH_XXX_SUBDIR|
.. |CMAKE_PREFIX_PATH_XXX| replace::
   ``<prefix>/include/<arch>`` if :variable:`CMAKE_LIBRARY_ARCHITECTURE`
   is set, and |CMAKE_PREFIX_PATH_XXX_SUBDIR|
.. |CMAKE_XXX_PATH| replace:: :variable:`CMAKE_INCLUDE_PATH`
.. |CMAKE_XXX_MAC_PATH| replace:: :variable:`CMAKE_FRAMEWORK_PATH`

.. |SYSTEM_ENVIRONMENT_PATH_XXX| replace:: The directories in ``INCLUDE``
   and ``PATH``.
.. |SYSTEM_ENVIRONMENT_PATH_WINDOWS_XXX| replace:: On Windows hosts:
      ``<prefix>/include/<arch>`` if :variable:`CMAKE_LIBRARY_ARCHITECTURE`
      is set, and |SYSTEM_ENVIRONMENT_PREFIX_PATH_XXX_SUBDIR|.

.. |CMAKE_SYSTEM_PREFIX_PATH_XXX| replace::
   ``<prefix>/include/<arch>`` if :variable:`CMAKE_LIBRARY_ARCHITECTURE`
   is set, and |CMAKE_SYSTEM_PREFIX_PATH_XXX_SUBDIR|
.. |CMAKE_SYSTEM_XXX_PATH| replace::
   :variable:`CMAKE_SYSTEM_INCLUDE_PATH`
.. |CMAKE_SYSTEM_XXX_MAC_PATH| replace::
   :variable:`CMAKE_SYSTEM_FRAMEWORK_PATH`

.. |CMAKE_FIND_ROOT_PATH_MODE_XXX| replace::
   :variable:`CMAKE_FIND_ROOT_PATH_MODE_INCLUDE`

.. include:: FIND_XXX.txt
