FAIL_REGULAR_EXPRESSION
-----------------------

If the output matches this regular expression the test will fail.

If set, if the output matches one of specified regular expressions,
the test will fail.  Example:

.. code-block:: cmake

  set_tests_properties(mytest PROPERTIES
    FAIL_REGULAR_EXPRESSION "[^a-z]Error;ERROR;Failed"
  )

``FAIL_REGULAR_EXPRESSION`` expects a list of regular expressions.
