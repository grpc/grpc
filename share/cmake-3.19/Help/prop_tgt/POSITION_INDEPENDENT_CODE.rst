POSITION_INDEPENDENT_CODE
-------------------------

Whether to create a position-independent target

The ``POSITION_INDEPENDENT_CODE`` property determines whether position
independent executables or shared libraries will be created.  This
property is ``True`` by default for ``SHARED`` and ``MODULE`` library
targets and ``False`` otherwise.  This property is initialized by the value
of the :variable:`CMAKE_POSITION_INDEPENDENT_CODE` variable  if it is set
when a target is created.

.. note::

  For executable targets, the link step is controlled by the :policy:`CMP0083`
  policy and the :module:`CheckPIESupported` module.
