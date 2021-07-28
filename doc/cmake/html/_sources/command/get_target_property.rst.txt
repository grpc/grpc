get_target_property
-------------------

Get a property from a target.

.. code-block:: cmake

  get_target_property(<VAR> target property)

Get a property from a target.  The value of the property is stored in
the variable ``<VAR>``.  If the target property is not found, the behavior
depends on whether it has been defined to be an ``INHERITED`` property
or not (see :command:`define_property`).  Non-inherited properties will
set ``<VAR>`` to ``<VAR>-NOTFOUND``, whereas inherited properties will search
the relevant parent scope as described for the :command:`define_property`
command and if still unable to find the property, ``<VAR>`` will be set to
an empty string.

Use :command:`set_target_properties` to set target property values.
Properties are usually used to control how a target is built, but some
query the target instead.  This command can get properties for any
target so far created.  The targets do not need to be in the current
``CMakeLists.txt`` file.

See also the more general :command:`get_property` command.

See :ref:`Target Properties` for the list of properties known to CMake.
