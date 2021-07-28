.. cmake-manual-description: CMakePresets.json

cmake-presets(7)
****************

.. only:: html

   .. contents::

Introduction
============

One problem that CMake users often face is sharing settings with other people
for common ways to configure a project. This may be done to support CI builds,
or for users who frequently use the same build. CMake supports two files,
``CMakePresets.json`` and ``CMakeUserPresets.json``, that allow users to
specify common configure options and share them with others.

``CMakePresets.json`` and ``CMakeUserPresets.json`` live in the project's root
directory. They both have exactly the same format, and both are optional
(though at least one must be present if ``--preset`` is specified.)
``CMakePresets.json`` is meant to save project-wide builds, while
``CMakeUserPresets.json`` is meant for developers to save their own local
builds. ``CMakePresets.json`` may be checked into a version control system, and
``CMakeUserPresets.json`` should NOT be checked in. For example, if a project
is using Git, ``CMakePresets.json`` may be tracked, and
``CMakeUserPresets.json`` should be added to the ``.gitignore``.

Format
======

  The files are a JSON document with an object as the root:

  .. literalinclude:: presets/example.json
    :language: json

  The root object recognizes the following fields:

  ``version``

    A required integer representing the version of the JSON schema. Currently,
    the only supported version is 1.

  ``cmakeMinimumRequired``

    An optional object representing the minimum version of CMake needed to
    build this project. This object consists of the following fields:

    ``major``

      An optional integer representing the major version.

    ``minor``

      An optional integer representing the minor version.

    ``patch``

      An optional integer representing the patch version.

  ``vendor``

    An optional map containing vendor-specific information. CMake does not
    interpret the contents of this field except to verify that it is a map if
    it does exist. However, the keys should be a vendor-specific domain name
    followed by a ``/``-separated path. For example, the Example IDE 1.0 could
    use ``example.com/ExampleIDE/1.0``. The value of each field can be anything
    desired by the vendor, though will typically be a map.

  ``configurePresets``

    An optional array of configure preset objects. Each preset may contain the
    following fields:

    ``name``

      A required string representing the machine-friendly name of the preset.
      This identifier is used in the ``--preset`` argument. There must not be
      two presets in the union of ``CMakePresets.json`` and
      ``CMakeUserPresets.json`` in the same directory with the same name.

    ``hidden``

      An optional boolean specifying whether or not a preset should be hidden.
      If a preset is hidden, it cannot be used in the ``--preset=`` argument,
      will not show up in the :manual:`CMake GUI <cmake-gui(1)>`, and does not
      have to have a valid ``generator`` or ``binaryDir``, even from
      inheritance. ``hidden`` presets are intended to be used as a base for
      other presets to inherit via the ``inherits`` field.

    ``inherits``

      An optional array of strings representing the names of presets to inherit
      from. The preset will inherit all of the fields from the ``inherits``
      presets by default (except ``name``, ``hidden``, ``inherits``,
      ``description``, and ``displayName``), but can override them as
      desired. If multiple ``inherits`` presets provide conflicting values for
      the same field, the earlier preset in the ``inherits`` list will be
      preferred. Presets in ``CMakePresets.json`` may not inherit from presets
      in ``CMakeUserPresets.json``.

      This field can also be a string, which is equivalent to an array
      containing one string.

    ``vendor``

      An optional map containing vendor-specific information. CMake does not
      interpret the contents of this field except to verify that it is a map
      if it does exist. However, it should follow the same conventions as the
      root-level ``vendor`` field. If vendors use their own per-preset
      ``vendor`` field, they should implement inheritance in a sensible manner
      when appropriate.

    ``displayName``

      An optional string with a human-friendly name of the preset.

    ``description``

      An optional string with a human-friendly description of the preset.

    ``generator``

      An optional string representing the generator to use for the preset. If
      ``generator`` is not specified, it must be inherited from the
      ``inherits`` preset (unless this preset is ``hidden``).

      Note that for Visual Studio generators, unlike in the command line ``-G``
      argument, you cannot include the platform name in the generator name. Use
      the ``architecture`` field instead.

    ``architecture``
    ``toolset``

      Optional fields representing the platform and toolset, respectively, for
      generators that support them. Each may be either a string or an object
      with the following fields:

      ``value``

        An optional string representing the value.

      ``strategy``

        An optional string telling CMake how to handle the ``architecture`` or
        ``toolset`` field. Valid values are:

        ``"set"``

          Set the respective value. This will result in an error for generators
          that do not support the respective field.

        ``"external"``

          Do not set the value, even if the generator supports it. This is
          useful if, for example, a preset uses the Ninja generator, and an IDE
          knows how to set up the Visual C++ environment from the
          ``architecture`` and ``toolset`` fields. In that case, CMake will
          ignore the field, but the IDE can use them to set up the environment
          before invoking CMake.

    ``binaryDir``

      An optional string representing the path to the output binary directory.
      This field supports macro expansion. If a relative path is specified, it
      is calculated relative to the source directory. If ``binaryDir`` is not
      specified, it must be inherited from the ``inherits`` preset (unless this
      preset is ``hidden``).

    ``cmakeExecutable``

      An optional string representing the path to the CMake executable to use
      for this preset. This is reserved for use by IDEs, and is not used by
      CMake itself. IDEs that use this field should expand any macros in it.

    ``cacheVariables``

      An optional map of cache variables. The key is the variable name (which
      may not be an empty string), and the value is either ``null``, a boolean
      (which is equivalent to a value of ``"TRUE"`` or ``"FALSE"`` and a type
      of ``BOOL``), a string representing the value of the variable (which
      supports macro expansion), or an object with the following fields:

      ``type``

        An optional string representing the type of the variable.

      ``value``

        A required string or boolean representing the value of the variable.
        A boolean is equivalent to ``"TRUE"`` or ``"FALSE"``. This field
        supports macro expansion.

      Cache variables are inherited through the ``inherits`` field, and the
      preset's variables will be the union of its own ``cacheVariables`` and
      the ``cacheVariables`` from all its parents. If multiple presets in this
      union define the same variable, the standard rules of ``inherits`` are
      applied. Setting a variable to ``null`` causes it to not be set, even if
      a value was inherited from another preset.

    ``environment``

      An optional map of environment variables. The key is the variable name
      (which may not be an empty string), and the value is either ``null`` or
      a string representing the value of the variable. Each variable is set
      regardless of whether or not a value was given to it by the process's
      environment. This field supports macro expansion, and environment
      variables in this map may reference each other, and may be listed in any
      order, as long as such references do not cause a cycle (for example,
      if ``ENV_1`` is ``$env{ENV_2}``, ``ENV_2`` may not be ``$env{ENV_1}``.)

      Environment variables are inherited through the ``inherits`` field, and
      the preset's environment will be the union of its own ``environment`` and
      the ``environment`` from all its parents. If multiple presets in this
      union define the same variable, the standard rules of ``inherits`` are
      applied. Setting a variable to ``null`` causes it to not be set, even if
      a value was inherited from another preset.

    ``warnings``

      An optional object specifying the warnings to enable. The object may
      contain the following fields:

      ``dev``

        An optional boolean. Equivalent to passing ``-Wdev`` or ``-Wno-dev``
        on the command line. This may not be set to ``false`` if ``errors.dev``
        is set to ``true``.

      ``deprecated``

        An optional boolean. Equivalent to passing ``-Wdeprecated`` or
        ``-Wno-deprecated`` on the command line. This may not be set to
        ``false`` if ``errors.deprecated`` is set to ``true``.

      ``uninitialized``

        An optional boolean. Setting this to ``true`` is equivalent to passing
        ``--warn-uninitialized`` on the command line.

      ``unusedCli``

        An optional boolean. Setting this to ``false`` is equivalent to passing
        ``--no-warn-unused-cli`` on the command line.

      ``systemVars``

        An optional boolean. Setting this to ``true`` is equivalent to passing
        ``--check-system-vars`` on the command line.

    ``errors``

      An optional object specifying the errors to enable. The object may
      contain the following fields:

      ``dev``

        An optional boolean. Equivalent to passing ``-Werror=dev`` or
        ``-Wno-error=dev`` on the command line. This may not be set to ``true``
        if ``warnings.dev`` is set to ``false``.

      ``deprecated``

        An optional boolean. Equivalent to passing ``-Werror=deprecated`` or
        ``-Wno-error=deprecated`` on the command line. This may not be set to
        ``true`` if ``warnings.deprecated`` is set to ``false``.

    ``debug``

      An optional object specifying debug options. The object may contain the
      following fields:

      ``output``

        An optional boolean. Setting this to ``true`` is equivalent to passing
        ``--debug-output`` on the command line.

      ``tryCompile``

        An optional boolean. Setting this to ``true`` is equivalent to passing
        ``--debug-trycompile`` on the command line.

      ``find``

        An optional boolean. Setting this to ``true`` is equivalent to passing
        ``--debug-find`` on the command line.

  As mentioned above, some fields support macro expansion. Macros are
  recognized in the form ``$<macro-namespace>{<macro-name>}``. All macros are
  evaluated in the context of the preset being used, even if the macro is in a
  field that was inherited from another preset. For example, if the ``Base``
  preset sets variable ``PRESET_NAME`` to ``${presetName}``, and the
  ``Derived`` preset inherits from ``Base``, ``PRESET_NAME`` will be set to
  ``Derived``.

  It is an error to not put a closing brace at the end of a macro name. For
  example, ``${sourceDir`` is invalid. A dollar sign (``$``) followed by
  anything other than a left curly brace (``{``) with a possible namespace is
  interpreted as a literal dollar sign.

  Recognized macros include:

  ``${sourceDir}``

    Path to the project source directory.

  ``${sourceParentDir}``

    Path to the project source directory's parent directory.

  ``${sourceDirName}``

    The last filename component of ``${sourceDir}``. For example, if
    ``${sourceDir}`` is ``/path/to/source``, this would be ``source``.

  ``${presetName}``

    Name specified in the preset's ``name`` field.

  ``${generator}``

    Generator specified in the preset's ``generator`` field.

  ``${dollar}``

    A literal dollar sign (``$``).

  ``$env{<variable-name>}``

    Environment variable with name ``<variable-name>``. The variable name may
    not be an empty string. If the variable is defined in the ``environment``
    field, that value is used instead of the value from the parent environment.
    If the environment variable is not defined, this evaluates as an empty
    string.

    Note that while Windows environment variable names are case-insensitive,
    variable names within a preset are still case-sensitive. This may lead to
    unexpected results when using inconsistent casing. For best results, keep
    the casing of environment variable names consistent.

  ``$penv{<variable-name>}``

    Similar to ``$env{<variable-name>}``, except that the value only comes from
    the parent environment, and never from the ``environment`` field. This
    allows you to prepend or append values to existing environment variables.
    For example, setting ``PATH`` to ``/path/to/ninja/bin:$penv{PATH}`` will
    prepend ``/path/to/ninja/bin`` to the ``PATH`` environment variable. This
    is needed because ``$env{<variable-name>}`` does not allow circular
    references.

  ``$vendor{<macro-name>}``

    An extension point for vendors to insert their own macros. CMake will not
    be able to use presets which have a ``$vendor{<macro-name>}`` macro, and
    effectively ignores such presets. However, it will still be able to use
    other presets from the same file.

    CMake does not make any attempt to interpret ``$vendor{<macro-name>}``
    macros. However, to avoid name collisions, IDE vendors should prefix
    ``<macro-name>`` with a very short (preferably <= 4 characters) vendor
    identifier prefix, followed by a ``.``, followed by the macro name. For
    example, the Example IDE could have ``$vendor{xide.ideInstallDir}``.

Schema
======

:download:`This file </manual/presets/schema.json>` provides a machine-readable
JSON schema for the ``CMakePresets.json`` format.
