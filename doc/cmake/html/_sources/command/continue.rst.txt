continue
--------

.. versionadded:: 3.2

Continue to the top of enclosing foreach or while loop.

.. code-block:: cmake

  continue()

The ``continue`` command allows a cmake script to abort the rest of a block
in a :command:`foreach` or :command:`while` loop, and start at the top of
the next iteration.

See also the :command:`break` command.
