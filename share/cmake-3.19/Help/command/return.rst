return
------

Return from a file, directory or function.

.. code-block:: cmake

  return()

Returns from a file, directory or function.  When this command is
encountered in an included file (via :command:`include` or
:command:`find_package`), it causes processing of the current file to stop
and control is returned to the including file.  If it is encountered in a
file which is not included by another file, e.g.  a ``CMakeLists.txt``,
deferred calls scheduled by :command:`cmake_language(DEFER)` are invoked and
control is returned to the parent directory if there is one.  If return is
called in a function, control is returned to the caller of the function.

Note that a :command:`macro <macro>`, unlike a :command:`function <function>`,
is expanded in place and therefore cannot handle ``return()``.
