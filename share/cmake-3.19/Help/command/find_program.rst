find_program
------------

.. |FIND_XXX| replace:: find_program
.. |NAMES| replace:: NAMES name1 [name2 ...] [NAMES_PER_DIR]
.. |SEARCH_XXX| replace:: program
.. |SEARCH_XXX_DESC| replace:: program
.. |prefix_XXX_SUBDIR| replace:: ``<prefix>/[s]bin``
.. |entry_XXX_SUBDIR| replace:: ``<entry>/[s]bin``

.. |FIND_PACKAGE_ROOT_PREFIX_PATH_XXX| replace::
   |FIND_PACKAGE_ROOT_PREFIX_PATH_XXX_SUBDIR|
.. |CMAKE_PREFIX_PATH_XXX| replace::
   |CMAKE_PREFIX_PATH_XXX_SUBDIR|
.. |CMAKE_XXX_PATH| replace:: :variable:`CMAKE_PROGRAM_PATH`
.. |CMAKE_XXX_MAC_PATH| replace:: :variable:`CMAKE_APPBUNDLE_PATH`

.. |SYSTEM_ENVIRONMENT_PATH_XXX| replace:: The directories in ``PATH`` itself.
.. |SYSTEM_ENVIRONMENT_PATH_WINDOWS_XXX| replace:: On Windows hosts no extra search paths are included

.. |CMAKE_SYSTEM_PREFIX_PATH_XXX| replace::
   |CMAKE_SYSTEM_PREFIX_PATH_XXX_SUBDIR|
.. |CMAKE_SYSTEM_XXX_PATH| replace::
   :variable:`CMAKE_SYSTEM_PROGRAM_PATH`
.. |CMAKE_SYSTEM_XXX_MAC_PATH| replace::
   :variable:`CMAKE_SYSTEM_APPBUNDLE_PATH`

.. |CMAKE_FIND_ROOT_PATH_MODE_XXX| replace::
   :variable:`CMAKE_FIND_ROOT_PATH_MODE_PROGRAM`

.. include:: FIND_XXX.txt

When more than one value is given to the ``NAMES`` option this command by
default will consider one name at a time and search every directory
for it.  The ``NAMES_PER_DIR`` option tells this command to consider one
directory at a time and search for all names in it.
