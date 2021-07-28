get_test_property
-----------------

Get a property of the test.

.. code-block:: cmake

  get_test_property(test property VAR)

Get a property from the test.  The value of the property is stored in
the variable ``VAR``.  If the test property is not found, the behavior
depends on whether it has been defined to be an ``INHERITED`` property
or not (see :command:`define_property`).  Non-inherited properties will
set ``VAR`` to "NOTFOUND", whereas inherited properties will search the
relevant parent scope as described for the :command:`define_property`
command and if still unable to find the property, ``VAR`` will be set to
an empty string.

For a list of standard properties you can type ``cmake --help-property-list``.

See also the more general :command:`get_property` command.
