cmake_minimum_required
----------------------

Require a minimum version of cmake.

.. code-block:: cmake

  cmake_minimum_required(VERSION <min>[...<max>] [FATAL_ERROR])

Sets the minimum required version of cmake for a project.
Also updates the policy settings as explained below.

``<min>`` and the optional ``<max>`` are each CMake versions of the form
``major.minor[.patch[.tweak]]``, and the ``...`` is literal.

If the running version of CMake is lower than the ``<min>`` required
version it will stop processing the project and report an error.
The optional ``<max>`` version, if specified, must be at least the
``<min>`` version and affects policy settings as described below.
If the running version of CMake is older than 3.12, the extra ``...``
dots will be seen as version component separators, resulting in the
``...<max>`` part being ignored and preserving the pre-3.12 behavior
of basing policies on ``<min>``.

This command will set the value of the
:variable:`CMAKE_MINIMUM_REQUIRED_VERSION` variable to ``<min>``.

The ``FATAL_ERROR`` option is accepted but ignored by CMake 2.6 and
higher.  It should be specified so CMake versions 2.4 and lower fail
with an error instead of just a warning.

.. note::
  Call the ``cmake_minimum_required()`` command at the beginning of
  the top-level ``CMakeLists.txt`` file even before calling the
  :command:`project` command.  It is important to establish version
  and policy settings before invoking other commands whose behavior
  they may affect.  See also policy :policy:`CMP0000`.

  Calling ``cmake_minimum_required()`` inside a :command:`function`
  limits some effects to the function scope when invoked.  Such calls
  should not be made with the intention of having global effects.

Policy Settings
^^^^^^^^^^^^^^^

The ``cmake_minimum_required(VERSION)`` command implicitly invokes the
:command:`cmake_policy(VERSION)` command to specify that the current
project code is written for the given range of CMake versions.
All policies known to the running version of CMake and introduced
in the ``<min>`` (or ``<max>``, if specified) version or earlier will
be set to use ``NEW`` behavior.  All policies introduced in later
versions will be unset.  This effectively requests behavior preferred
as of a given CMake version and tells newer CMake versions to warn
about their new policies.

When a ``<min>`` version higher than 2.4 is specified the command
implicitly invokes

.. code-block:: cmake

  cmake_policy(VERSION <min>[...<max>])

which sets CMake policies based on the range of versions specified.
When a ``<min>`` version 2.4 or lower is given the command implicitly
invokes

.. code-block:: cmake

  cmake_policy(VERSION 2.4[...<max>])

which enables compatibility features for CMake 2.4 and lower.
