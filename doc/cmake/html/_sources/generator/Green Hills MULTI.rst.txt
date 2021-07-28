Green Hills MULTI
-----------------

.. versionadded:: 3.3

Generates Green Hills MULTI project files (experimental, work-in-progress).

The buildsystem has predetermined build-configuration settings that can be controlled
via the :variable:`CMAKE_BUILD_TYPE` variable.

Customizations that are used to pick toolset and target system:

The ``-A <arch>`` can be supplied for setting the target architecture.
``<arch>`` usually is one of ``arm``, ``ppc``, ``86``, etcetera.
If the target architecture is not specified then
the default architecture of ``arm`` will be used.

The ``-T <toolset>`` option can be used to set the directory location of the toolset.
Both absolute and relative paths are valid. Relative paths use ``GHS_TOOLSET_ROOT``
as the root. If the toolset is not specified then the latest toolset found in
``GHS_TOOLSET_ROOT`` will be used.

Cache variables that are used for toolset and target system customization:

* ``GHS_TARGET_PLATFORM``

  | Defaults to ``integrity``.
  | Usual values are ``integrity``, ``threadx``, ``uvelosity``, ``velosity``,
    ``vxworks``, ``standalone``.

* ``GHS_PRIMARY_TARGET``

  | Sets ``primaryTarget`` entry in project file.
  | Defaults to ``<arch>_<GHS_TARGET_PLATFORM>.tgt``.

* ``GHS_TOOLSET_ROOT``

  | Root path for ``toolset`` searches.
  | Defaults to ``C:/ghs`` in Windows or ``/usr/ghs`` in Linux.

* ``GHS_OS_ROOT``

  | Root path for RTOS searches.
  | Defaults to ``C:/ghs`` in Windows or ``/usr/ghs`` in Linux.

* ``GHS_OS_DIR`` and ``GHS_OS_DIR_OPTION``

  | Sets ``-os_dir`` entry in project file.
  | Defaults to latest platform OS installation at ``GHS_OS_ROOT``.  Set this value if
    a specific RTOS is to be used.
  | ``GHS_OS_DIR_OPTION`` default value is ``-os_dir``.

* ``GHS_BSP_NAME``

  | Sets ``-bsp`` entry in project file.
  | Defaults to ``sim<arch>`` for ``integrity`` platforms.

Customizations are available through the following cache variables:

* ``GHS_CUSTOMIZATION``
* ``GHS_GPJ_MACROS``

The following properties are available:

* :prop_tgt:`GHS_INTEGRITY_APP`
* :prop_tgt:`GHS_NO_SOURCE_GROUP_FILE`

.. note::
  This generator is deemed experimental as of CMake |release|
  and is still a work in progress.  Future versions of CMake
  may make breaking changes as the generator matures.
