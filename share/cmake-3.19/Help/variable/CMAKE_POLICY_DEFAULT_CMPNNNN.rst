CMAKE_POLICY_DEFAULT_CMP<NNNN>
------------------------------

Default for CMake Policy ``CMP<NNNN>`` when it is otherwise left unset.

Commands :command:`cmake_minimum_required(VERSION)` and
:command:`cmake_policy(VERSION)` by default leave policies introduced after
the given version unset.  Set ``CMAKE_POLICY_DEFAULT_CMP<NNNN>`` to ``OLD``
or ``NEW`` to specify the default for policy ``CMP<NNNN>``, where ``<NNNN>``
is the policy number.

This variable should not be set by a project in CMake code; use
:command:`cmake_policy(SET)` instead.  Users running CMake may set this
variable in the cache (e.g. ``-DCMAKE_POLICY_DEFAULT_CMP<NNNN>=<OLD|NEW>``)
to set a policy not otherwise set by the project.  Set to ``OLD`` to quiet a
policy warning while using old behavior or to ``NEW`` to try building the
project with new behavior.
