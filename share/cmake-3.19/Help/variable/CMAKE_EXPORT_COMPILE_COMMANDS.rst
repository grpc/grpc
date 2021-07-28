CMAKE_EXPORT_COMPILE_COMMANDS
-----------------------------

.. versionadded:: 3.5

Enable/Disable output of compile commands during generation.

If enabled, generates a ``compile_commands.json`` file containing the exact
compiler calls for all translation units of the project in machine-readable
form.  The format of the JSON file looks like:

.. code-block:: javascript

  [
    {
      "directory": "/home/user/development/project",
      "command": "/usr/bin/c++ ... -c ../foo/foo.cc",
      "file": "../foo/foo.cc"
    },

    ...

    {
      "directory": "/home/user/development/project",
      "command": "/usr/bin/c++ ... -c ../foo/bar.cc",
      "file": "../foo/bar.cc"
    }
  ]

This is initialized by the :envvar:`CMAKE_EXPORT_COMPILE_COMMANDS` environment
variable.

.. note::
  This option is implemented only by :ref:`Makefile Generators`
  and the :generator:`Ninja`.  It is ignored on other generators.

  This option currently does not work well in combination with
  the :prop_tgt:`UNITY_BUILD` target property or the
  :variable:`CMAKE_UNITY_BUILD` variable.
