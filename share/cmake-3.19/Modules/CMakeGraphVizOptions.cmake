# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CMakeGraphVizOptions
--------------------

The builtin Graphviz support of CMake.

Generating Graphviz files
^^^^^^^^^^^^^^^^^^^^^^^^^

CMake can generate `Graphviz <https://www.graphviz.org/>`_ files showing the
dependencies between the targets in a project, as well as external libraries
which are linked against.

When running CMake with the ``--graphviz=foo.dot`` option, it produces:

* a ``foo.dot`` file, showing all dependencies in the project
* a ``foo.dot.<target>`` file for each target, showing on which other targets
  it depends
* a ``foo.dot.<target>.dependers`` file for each target, showing which other
  targets depend on it

Those .dot files can be converted to images using the *dot* command from the
Graphviz package:

.. code-block:: shell

  dot -Tpng -o foo.png foo.dot

The different dependency types ``PUBLIC``, ``INTERFACE`` and ``PRIVATE``
are represented as solid, dashed and dotted edges.

Variables specific to the Graphviz support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The resulting graphs can be huge.  The look and content of the generated graphs
can be controlled using the file ``CMakeGraphVizOptions.cmake``.  This file is
first searched in :variable:`CMAKE_BINARY_DIR`, and then in
:variable:`CMAKE_SOURCE_DIR`.  If found, the variables set in it are used to
adjust options for the generated Graphviz files.

.. variable:: GRAPHVIZ_GRAPH_NAME

 The graph name.

 * Mandatory: NO
 * Default: value of :variable:`CMAKE_PROJECT_NAME`

.. variable:: GRAPHVIZ_GRAPH_HEADER

 The header written at the top of the Graphviz files.

 * Mandatory: NO
 * Default: "node [ fontsize = "12" ];"

.. variable:: GRAPHVIZ_NODE_PREFIX

 The prefix for each node in the Graphviz files.

 * Mandatory: NO
 * Default: "node"

.. variable:: GRAPHVIZ_EXECUTABLES

 Set to FALSE to exclude executables from the generated graphs.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_STATIC_LIBS

 Set to FALSE to exclude static libraries from the generated graphs.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_SHARED_LIBS

 Set to FALSE to exclude shared libraries from the generated graphs.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_MODULE_LIBS

 Set to FALSE to exclude module libraries from the generated graphs.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_INTERFACE_LIBS

 Set to FALSE to exclude interface libraries from the generated graphs.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_OBJECT_LIBS

 Set to FALSE to exclude object libraries from the generated graphs.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_UNKNOWN_LIBS

 Set to FALSE to exclude unknown libraries from the generated graphs.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_EXTERNAL_LIBS

 Set to FALSE to exclude external libraries from the generated graphs.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_CUSTOM_TARGETS

 Set to TRUE to include custom targets in the generated graphs.

 * Mandatory: NO
 * Default: FALSE

.. variable:: GRAPHVIZ_IGNORE_TARGETS

 A list of regular expressions for names of targets to exclude from the
 generated graphs.

 * Mandatory: NO
 * Default: empty

.. variable:: GRAPHVIZ_GENERATE_PER_TARGET

 Set to FALSE to not generate per-target graphs ``foo.dot.<target>``.

 * Mandatory: NO
 * Default: TRUE

.. variable:: GRAPHVIZ_GENERATE_DEPENDERS

 Set to FALSE to not generate depender graphs ``foo.dot.<target>.dependers``.

 * Mandatory: NO
 * Default: TRUE
#]=======================================================================]
