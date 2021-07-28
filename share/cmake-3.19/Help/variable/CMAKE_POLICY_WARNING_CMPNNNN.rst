CMAKE_POLICY_WARNING_CMP<NNNN>
------------------------------

Explicitly enable or disable the warning when CMake Policy ``CMP<NNNN>``
is not set.  This is meaningful only for the few policies that do not
warn by default:

* ``CMAKE_POLICY_WARNING_CMP0025`` controls the warning for
  policy :policy:`CMP0025`.
* ``CMAKE_POLICY_WARNING_CMP0047`` controls the warning for
  policy :policy:`CMP0047`.
* ``CMAKE_POLICY_WARNING_CMP0056`` controls the warning for
  policy :policy:`CMP0056`.
* ``CMAKE_POLICY_WARNING_CMP0060`` controls the warning for
  policy :policy:`CMP0060`.
* ``CMAKE_POLICY_WARNING_CMP0065`` controls the warning for
  policy :policy:`CMP0065`.
* ``CMAKE_POLICY_WARNING_CMP0066`` controls the warning for
  policy :policy:`CMP0066`.
* ``CMAKE_POLICY_WARNING_CMP0067`` controls the warning for
  policy :policy:`CMP0067`.
* ``CMAKE_POLICY_WARNING_CMP0082`` controls the warning for
  policy :policy:`CMP0082`.
* ``CMAKE_POLICY_WARNING_CMP0089`` controls the warning for
  policy :policy:`CMP0089`.
* ``CMAKE_POLICY_WARNING_CMP0102`` controls the warning for
  policy :policy:`CMP0102`.
* ``CMAKE_POLICY_WARNING_CMP0112`` controls the warning for
  policy :policy:`CMP0112`.

This variable should not be set by a project in CMake code.  Project
developers running CMake may set this variable in their cache to
enable the warning (e.g. ``-DCMAKE_POLICY_WARNING_CMP<NNNN>=ON``).
Alternatively, running :manual:`cmake(1)` with the ``--debug-output``,
``--trace``, or ``--trace-expand`` option will also enable the warning.
