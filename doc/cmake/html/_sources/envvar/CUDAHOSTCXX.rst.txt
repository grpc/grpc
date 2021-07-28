CUDAHOSTCXX
-----------

.. versionadded:: 3.8

.. include:: ENV_VAR.txt

Preferred executable for compiling host code when compiling ``CUDA``
language files. Will only be used by CMake on the first configuration to
determine ``CUDA`` host compiler, after which the value for ``CUDAHOSTCXX`` is
stored in the cache as :variable:`CMAKE_CUDA_HOST_COMPILER`. For any
configuration run (including the first), the environment variable will be
ignored if the :variable:`CMAKE_CUDA_HOST_COMPILER` variable is defined.

This environment variable is primarily meant for use with projects that
enable ``CUDA`` as a first-class language.  The :module:`FindCUDA`
module will also use it to initialize its ``CUDA_HOST_COMPILER`` setting.
