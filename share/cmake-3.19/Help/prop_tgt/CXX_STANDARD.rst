CXX_STANDARD
------------

.. versionadded:: 3.1

The C++ standard whose features are requested to build this target.

This property specifies the C++ standard whose features are requested
to build this target.  For some compilers, this results in adding a
flag such as ``-std=gnu++11`` to the compile line.  For compilers that
have no notion of a standard level, such as Microsoft Visual C++ before
2015 Update 3, this has no effect.

Supported values are ``98``, ``11``, ``14``, ``17``, and ``20``.

If the value requested does not result in a compile flag being added for
the compiler in use, a previous standard flag will be added instead.  This
means that using:

.. code-block:: cmake

  set_property(TARGET tgt PROPERTY CXX_STANDARD 11)

with a compiler which does not support ``-std=gnu++11`` or an equivalent
flag will not result in an error or warning, but will instead add the
``-std=gnu++98`` flag if supported.  This "decay" behavior may be controlled
with the :prop_tgt:`CXX_STANDARD_REQUIRED` target property.
Additionally, the :prop_tgt:`CXX_EXTENSIONS` target property may be used to
control whether compiler-specific extensions are enabled on a per-target basis.

See the :manual:`cmake-compile-features(7)` manual for information on
compile features and a list of supported compilers.

This property is initialized by the value of
the :variable:`CMAKE_CXX_STANDARD` variable if it is set when a target
is created.
