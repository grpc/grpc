SKIP_RETURN_CODE
----------------

Return code to mark a test as skipped.

Sometimes only a test itself can determine if all requirements for the
test are met. If such a situation should not be considered a hard failure
a return code of the process can be specified that will mark the test as
``Not Run`` if it is encountered. Valid values are in the range of
0 to 255, inclusive.

See also the :prop_test:`SKIP_REGULAR_EXPRESSION` property.
