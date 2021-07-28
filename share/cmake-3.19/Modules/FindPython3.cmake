# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindPython3
-----------

.. versionadded:: 3.12

Find Python 3 interpreter, compiler and development environment (include
directories and libraries).

When a version is requested, it can be specified as a simple value or as a
range. For a detailed description of version range usage and capabilities,
refer to the :command:`find_package` command.

The following components are supported:

* ``Interpreter``: search for Python 3 interpreter
* ``Compiler``: search for Python 3 compiler. Only offered by IronPython.
* ``Development``: search for development artifacts (include directories and
  libraries). This component includes two sub-components which can be specified
  independently:

  * ``Development.Module``: search for artifacts for Python 3 module
    developments.
  * ``Development.Embed``: search for artifacts for Python 3 embedding
    developments.

* ``NumPy``: search for NumPy include directories.

If no ``COMPONENTS`` are specified, ``Interpreter`` is assumed.

If component ``Development`` is specified, it implies sub-components
``Development.Module`` and ``Development.Embed``.

To ensure consistent versions between components ``Interpreter``, ``Compiler``,
``Development`` (or one of its sub-components) and ``NumPy``, specify all
components at the same time::

  find_package (Python3 COMPONENTS Interpreter Development)

This module looks only for version 3 of Python. This module can be used
concurrently with :module:`FindPython2` module to use both Python versions.

The :module:`FindPython` module can be used if Python version does not matter
for you.

.. note::

  If components ``Interpreter`` and ``Development`` (or one of its
  sub-components) are both specified, this module search only for interpreter
  with same platform architecture as the one defined by ``CMake``
  configuration. This constraint does not apply if only ``Interpreter``
  component is specified.

Imported Targets
^^^^^^^^^^^^^^^^

This module defines the following :ref:`Imported Targets <Imported Targets>`
(when :prop_gbl:`CMAKE_ROLE` is ``PROJECT``):

``Python3::Interpreter``
  Python 3 interpreter. Target defined if component ``Interpreter`` is found.
``Python3::Compiler``
  Python 3 compiler. Target defined if component ``Compiler`` is found.
``Python3::Module``
  Python 3 library for Python module. Target defined if component
  ``Development.Module`` is found.
``Python3::Python``
  Python 3 library for Python embedding. Target defined if component
  ``Development.Embed`` is found.
``Python3::NumPy``
  NumPy library for Python 3. Target defined if component ``NumPy`` is found.

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project
(see :ref:`Standard Variable Names <CMake Developer Standard Variable Names>`):

``Python3_FOUND``
  System has the Python 3 requested components.
``Python3_Interpreter_FOUND``
  System has the Python 3 interpreter.
``Python3_EXECUTABLE``
  Path to the Python 3 interpreter.
``Python3_INTERPRETER_ID``
  A short string unique to the interpreter. Possible values include:
    * Python
    * ActivePython
    * Anaconda
    * Canopy
    * IronPython
    * PyPy
``Python3_STDLIB``
  Standard platform independent installation directory.

  Information returned by
  ``distutils.sysconfig.get_python_lib(plat_specific=False,standard_lib=True)``
  or else ``sysconfig.get_path('stdlib')``.
``Python3_STDARCH``
  Standard platform dependent installation directory.

  Information returned by
  ``distutils.sysconfig.get_python_lib(plat_specific=True,standard_lib=True)``
  or else ``sysconfig.get_path('platstdlib')``.
``Python3_SITELIB``
  Third-party platform independent installation directory.

  Information returned by
  ``distutils.sysconfig.get_python_lib(plat_specific=False,standard_lib=False)``
  or else ``sysconfig.get_path('purelib')``.
``Python3_SITEARCH``
  Third-party platform dependent installation directory.

  Information returned by
  ``distutils.sysconfig.get_python_lib(plat_specific=True,standard_lib=False)``
  or else ``sysconfig.get_path('platlib')``.
``Python3_SOABI``
  Extension suffix for modules.

  Information returned by
  ``distutils.sysconfig.get_config_var('SOABI')`` or computed from
  ``distutils.sysconfig.get_config_var('EXT_SUFFIX')`` or
  ``python3-config --extension-suffix``. If package ``distutils.sysconfig`` is
  not available, ``sysconfig.get_config_var('SOABI')`` or
  ``sysconfig.get_config_var('EXT_SUFFIX')`` are used.
``Python3_Compiler_FOUND``
  System has the Python 3 compiler.
``Python3_COMPILER``
  Path to the Python 3 compiler. Only offered by IronPython.
``Python3_COMPILER_ID``
  A short string unique to the compiler. Possible values include:
    * IronPython
``Python3_DOTNET_LAUNCHER``
  The ``.Net`` interpreter. Only used by ``IronPython`` implementation.
``Python3_Development_FOUND``
  System has the Python 3 development artifacts.
``Python3_Development.Module_FOUND``
  System has the Python 3 development artifacts for Python module.
``Python3_Development.Embed_FOUND``
  System has the Python 3 development artifacts for Python embedding.
``Python3_INCLUDE_DIRS``
  The Python 3 include directories.
``Python3_LINK_OPTIONS``
  The Python 3 link options. Some configurations require specific link options
  for a correct build and execution.
``Python3_LIBRARIES``
  The Python 3 libraries.
``Python3_LIBRARY_DIRS``
  The Python 3 library directories.
``Python3_RUNTIME_LIBRARY_DIRS``
  The Python 3 runtime library directories.
``Python3_VERSION``
  Python 3 version.
``Python3_VERSION_MAJOR``
  Python 3 major version.
``Python3_VERSION_MINOR``
  Python 3 minor version.
``Python3_VERSION_PATCH``
  Python 3 patch version.
``Python3_PyPy_VERSION``
  Python 3 PyPy version.
``Python3_NumPy_FOUND``
  System has the NumPy.
``Python3_NumPy_INCLUDE_DIRS``
  The NumPy include directories.
``Python3_NumPy_VERSION``
  The NumPy version.

Hints
^^^^^

``Python3_ROOT_DIR``
  Define the root directory of a Python 3 installation.

``Python3_USE_STATIC_LIBS``
  * If not defined, search for shared libraries and static libraries in that
    order.
  * If set to TRUE, search **only** for static libraries.
  * If set to FALSE, search **only** for shared libraries.

``Python3_FIND_ABI``
  This variable defines which ABIs, as defined in
  `PEP 3149 <https://www.python.org/dev/peps/pep-3149/>`_, should be searched.

  .. note::

    If ``Python3_FIND_ABI`` is not defined, any ABI will be searched.

  The ``Python3_FIND_ABI`` variable is a 3-tuple specifying, in that order,
  ``pydebug`` (``d``), ``pymalloc`` (``m``) and ``unicode`` (``u``) flags.
  Each element can be set to one of the following:

  * ``ON``: Corresponding flag is selected.
  * ``OFF``: Corresponding flag is not selected.
  * ``ANY``: The two possibilities (``ON`` and ``OFF``) will be searched.

  From this 3-tuple, various ABIs will be searched starting from the most
  specialized to the most general. Moreover, ``debug`` versions will be
  searched **after** ``non-debug`` ones.

  For example, if we have::

    set (Python3_FIND_ABI "ON" "ANY" "ANY")

  The following flags combinations will be appended, in that order, to the
  artifact names: ``dmu``, ``dm``, ``du``, and ``d``.

  And to search any possible ABIs::

    set (Python3_FIND_ABI "ANY" "ANY" "ANY")

  The following combinations, in that order, will be used: ``mu``, ``m``,
  ``u``, ``<empty>``, ``dmu``, ``dm``, ``du`` and ``d``.

  .. note::

    This hint is useful only on ``POSIX`` systems. So, on ``Windows`` systems,
    when ``Python3_FIND_ABI`` is defined, ``Python`` distributions from
    `python.org <https://www.python.org/>`_ will be found only if value for
    each flag is ``OFF`` or ``ANY``.

``Python3_FIND_STRATEGY``
  This variable defines how lookup will be done.
  The ``Python3_FIND_STRATEGY`` variable can be set to one of the following:

  * ``VERSION``: Try to find the most recent version in all specified
    locations.
    This is the default if policy :policy:`CMP0094` is undefined or set to
    ``OLD``.
  * ``LOCATION``: Stops lookup as soon as a version satisfying version
    constraints is founded.
    This is the default if policy :policy:`CMP0094` is set to ``NEW``.

``Python3_FIND_REGISTRY``
  On Windows the ``Python3_FIND_REGISTRY`` variable determine the order
  of preference between registry and environment variables.
  The ``Python3_FIND_REGISTRY`` variable can be set to one of the following:

  * ``FIRST``: Try to use registry before environment variables.
    This is the default.
  * ``LAST``: Try to use registry after environment variables.
  * ``NEVER``: Never try to use registry.

``Python3_FIND_FRAMEWORK``
  On macOS the ``Python3_FIND_FRAMEWORK`` variable determine the order of
  preference between Apple-style and unix-style package components.
  This variable can take same values as :variable:`CMAKE_FIND_FRAMEWORK`
  variable.

  .. note::

    Value ``ONLY`` is not supported so ``FIRST`` will be used instead.

  If ``Python3_FIND_FRAMEWORK`` is not defined, :variable:`CMAKE_FIND_FRAMEWORK`
  variable will be used, if any.

``Python3_FIND_VIRTUALENV``
  This variable defines the handling of virtual environments managed by
  ``virtualenv`` or ``conda``. It is meaningful only when a virtual environment
  is active (i.e. the ``activate`` script has been evaluated). In this case, it
  takes precedence over ``Python3_FIND_REGISTRY`` and ``CMAKE_FIND_FRAMEWORK``
  variables.  The ``Python3_FIND_VIRTUALENV`` variable can be set to one of the
  following:

  * ``FIRST``: The virtual environment is used before any other standard
    paths to look-up for the interpreter. This is the default.
  * ``ONLY``: Only the virtual environment is used to look-up for the
    interpreter.
  * ``STANDARD``: The virtual environment is not used to look-up for the
    interpreter but environment variable ``PATH`` is always considered.
    In this case, variable ``Python3_FIND_REGISTRY`` (Windows) or
    ``CMAKE_FIND_FRAMEWORK`` (macOS) can be set with value ``LAST`` or
    ``NEVER`` to select preferably the interpreter from the virtual
    environment.

  .. note::

    If the component ``Development`` is requested, it is **strongly**
    recommended to also include the component ``Interpreter`` to get expected
    result.

``Python3_FIND_IMPLEMENTATIONS``
  This variable defines, in an ordered list, the different implementations
  which will be searched. The ``Python3_FIND_IMPLEMENTATIONS`` variable can
  hold the following values:

  * ``CPython``: this is the standard implementation. Various products, like
    ``Anaconda`` or ``ActivePython``, rely on this implementation.
  * ``IronPython``: This implementation use the ``CSharp`` language for
    ``.NET Framework`` on top of the `Dynamic Language Runtime` (``DLR``).
    See `IronPython <http://ironpython.net>`_.
  * ``PyPy``: This implementation use ``RPython`` language and
    ``RPython translation toolchain`` to produce the python interpreter.
    See `PyPy <https://www.pypy.org>`_.

  The default value is:

  * Windows platform: ``CPython``, ``IronPython``
  * Other platforms: ``CPython``

  .. note::

    This hint has the lowest priority of all hints, so even if, for example,
    you specify ``IronPython`` first and ``CPython`` in second, a python
    product based on ``CPython`` can be selected because, for example with
    ``Python3_FIND_STRATEGY=LOCATION``, each location will be search first for
    ``IronPython`` and second for ``CPython``.

  .. note::

    When ``IronPython`` is specified, on platforms other than ``Windows``, the
    ``.Net`` interpreter (i.e. ``mono`` command) is expected to be available
    through the ``PATH`` variable.

Artifacts Specification
^^^^^^^^^^^^^^^^^^^^^^^

To solve special cases, it is possible to specify directly the artifacts by
setting the following variables:

``Python3_EXECUTABLE``
  The path to the interpreter.

``Python3_COMPILER``
  The path to the compiler.

``Python3_DOTNET_LAUNCHER``
  The ``.Net`` interpreter. Only used by ``IronPython`` implementation.

``Python3_LIBRARY``
  The path to the library. It will be used to compute the
  variables ``Python3_LIBRARIES``, ``Python3_LIBRARY_DIRS`` and
  ``Python3_RUNTIME_LIBRARY_DIRS``.

``Python3_INCLUDE_DIR``
  The path to the directory of the ``Python`` headers. It will be used to
  compute the variable ``Python3_INCLUDE_DIRS``.

``Python3_NumPy_INCLUDE_DIR``
  The path to the directory of the ``NumPy`` headers. It will be used to
  compute the variable ``Python3_NumPy_INCLUDE_DIRS``.

.. note::

  All paths must be absolute. Any artifact specified with a relative path
  will be ignored.

.. note::

  When an artifact is specified, all ``HINTS`` will be ignored and no search
  will be performed for this artifact.

  If more than one artifact is specified, it is the user's responsibility to
  ensure the consistency of the various artifacts.

By default, this module supports multiple calls in different directories of a
project with different version/component requirements while providing correct
and consistent results for each call. To support this behavior, ``CMake`` cache
is not used in the traditional way which can be problematic for interactive
specification. So, to enable also interactive specification, module behavior
can be controlled with the following variable:

``Python3_ARTIFACTS_INTERACTIVE``
  Selects the behavior of the module. This is a boolean variable:

  * If set to ``TRUE``: Create CMake cache entries for the above artifact
    specification variables so that users can edit them interactively.
    This disables support for multiple version/component requirements.
  * If set to ``FALSE`` or undefined: Enable multiple version/component
    requirements.

Commands
^^^^^^^^

This module defines the command ``Python3_add_library`` (when
:prop_gbl:`CMAKE_ROLE` is ``PROJECT``), which has the same semantics as
:command:`add_library` and adds a dependency to target ``Python3::Python`` or,
when library type is ``MODULE``, to target ``Python3::Module`` and takes care
of Python module naming rules::

  Python3_add_library (<name> [STATIC | SHARED | MODULE [WITH_SOABI]]
                       <source1> [<source2> ...])

If the library type is not specified, ``MODULE`` is assumed.

For ``MODULE`` library type, if option ``WITH_SOABI`` is specified, the
module suffix will include the ``Python3_SOABI`` value, if any.
#]=======================================================================]


set (_PYTHON_PREFIX Python3)

set (_Python3_REQUIRED_VERSION_MAJOR 3)

include (${CMAKE_CURRENT_LIST_DIR}/FindPython/Support.cmake)

if (COMMAND __Python3_add_library)
  macro (Python3_add_library)
    __Python3_add_library (Python3 ${ARGV})
  endmacro()
endif()

unset (_PYTHON_PREFIX)
