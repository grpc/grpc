CMAKE_CUDA_ARCHITECTURES
------------------------

.. versionadded:: 3.18

Default value for :prop_tgt:`CUDA_ARCHITECTURES` property of targets.

This is initialized as follows depending on :variable:`CMAKE_CUDA_COMPILER_ID <CMAKE_<LANG>_COMPILER_ID>`:

- For ``Clang``: the oldest architecture that works.

- For ``NVIDIA``: the default architecture chosen by the compiler.
  See policy :policy:`CMP0104`.

Users are encouraged to override this, as the default varies across compilers
and compiler versions.

This variable is used to initialize the :prop_tgt:`CUDA_ARCHITECTURES` property
on all targets. See the target property for additional information.
