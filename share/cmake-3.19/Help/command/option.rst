option
------

Provide an option that the user can optionally select.

.. code-block:: cmake

  option(<variable> "<help_text>" [value])

Provides an option for the user to select as ``ON`` or ``OFF``.
If no initial ``<value>`` is provided, ``OFF`` is used.
If ``<variable>`` is already set as a normal or cache variable,
then the command does nothing (see policy :policy:`CMP0077`).

If you have options that depend on the values of other options, see
the module help for :module:`CMakeDependentOption`.
