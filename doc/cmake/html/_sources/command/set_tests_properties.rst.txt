set_tests_properties
--------------------

Set a property of the tests.

.. code-block:: cmake

  set_tests_properties(test1 [test2...] PROPERTIES prop1 value1 prop2 value2)

Sets a property for the tests.  If the test is not found, CMake
will report an error.
:manual:`Generator expressions <cmake-generator-expressions(7)>` will be
expanded the same as supported by the test's :command:`add_test` call.

See also the :command:`set_property(TEST)` command.

See :ref:`Test Properties` for the list of properties known to CMake.
