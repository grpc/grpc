ctest_run_script
----------------

runs a ctest -S script

::

  ctest_run_script([NEW_PROCESS] script_file_name script_file_name1
              script_file_name2 ... [RETURN_VALUE var])

Runs a script or scripts much like if it was run from ctest -S.  If no
argument is provided then the current script is run using the current
settings of the variables.  If ``NEW_PROCESS`` is specified then each
script will be run in a separate process.If ``RETURN_VALUE`` is specified
the return value of the last script run will be put into ``var``.
