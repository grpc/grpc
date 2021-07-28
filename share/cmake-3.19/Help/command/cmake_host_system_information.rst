cmake_host_system_information
-----------------------------

Query host system specific information.

.. code-block:: cmake

  cmake_host_system_information(RESULT <variable> QUERY <key> ...)

Queries system information of the host system on which cmake runs.
One or more ``<key>`` can be provided to select the information to be
queried.  The list of queried values is stored in ``<variable>``.

``<key>`` can be one of the following values:

============================= ================================================
Key                           Description
============================= ================================================
``NUMBER_OF_LOGICAL_CORES``   Number of logical cores
``NUMBER_OF_PHYSICAL_CORES``  Number of physical cores
``HOSTNAME``                  Hostname
``FQDN``                      Fully qualified domain name
``TOTAL_VIRTUAL_MEMORY``      Total virtual memory in MiB [#mebibytes]_
``AVAILABLE_VIRTUAL_MEMORY``  Available virtual memory in MiB [#mebibytes]_
``TOTAL_PHYSICAL_MEMORY``     Total physical memory in MiB [#mebibytes]_
``AVAILABLE_PHYSICAL_MEMORY`` Available physical memory in MiB [#mebibytes]_
``IS_64BIT``                  One if processor is 64Bit
``HAS_FPU``                   One if processor has floating point unit
``HAS_MMX``                   One if processor supports MMX instructions
``HAS_MMX_PLUS``              One if processor supports Ext. MMX instructions
``HAS_SSE``                   One if processor supports SSE instructions
``HAS_SSE2``                  One if processor supports SSE2 instructions
``HAS_SSE_FP``                One if processor supports SSE FP instructions
``HAS_SSE_MMX``               One if processor supports SSE MMX instructions
``HAS_AMD_3DNOW``             One if processor supports 3DNow instructions
``HAS_AMD_3DNOW_PLUS``        One if processor supports 3DNow+ instructions
``HAS_IA64``                  One if IA64 processor emulating x86
``HAS_SERIAL_NUMBER``         One if processor has serial number
``PROCESSOR_SERIAL_NUMBER``   Processor serial number
``PROCESSOR_NAME``            Human readable processor name
``PROCESSOR_DESCRIPTION``     Human readable full processor description
``OS_NAME``                   See :variable:`CMAKE_HOST_SYSTEM_NAME`
``OS_RELEASE``                The OS sub-type e.g. on Windows ``Professional``
``OS_VERSION``                The OS build ID
``OS_PLATFORM``               See :variable:`CMAKE_HOST_SYSTEM_PROCESSOR`
============================= ================================================

.. rubric:: Footnotes

.. [#mebibytes] One MiB (mebibyte) is equal to 1024x1024 bytes.
