CUDA_RESOLVE_DEVICE_SYMBOLS
---------------------------

.. versionadded:: 3.9

CUDA only: Enables device linking for the specific library target where
required.

If set, this will tell the required compilers to enable device linking
on the library target. Device linking is an additional link step
required by some CUDA compilers when :prop_tgt:`CUDA_SEPARABLE_COMPILATION` is
enabled. Normally device linking is deferred until a shared library or
executable is generated, allowing for multiple static libraries to resolve
device symbols at the same time when they are used by a shared library or
executable.

By default static library targets have this property is disabled,
while shared, module, and executable targets have this property enabled.

Note that device linking is not supported for :ref:`Object Libraries`.


For instance:

.. code-block:: cmake

  set_property(TARGET mystaticlib PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)
