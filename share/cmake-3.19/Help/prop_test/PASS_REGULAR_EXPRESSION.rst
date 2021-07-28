PASS_REGULAR_EXPRESSION
-----------------------

The output must match this regular expression for the test to pass.

If set, the test output will be checked against the specified regular
expressions and at least one of the regular expressions has to match,
otherwise the test will fail.  Example:

.. code-block:: cmake

  set_tests_properties(mytest PROPERTIES
    PASS_REGULAR_EXPRESSION "TestPassed;All ok"
  )

``PASS_REGULAR_EXPRESSION`` expects a list of regular expressions.
