VS_DEBUGGER_WORKING_DIRECTORY
-----------------------------

.. versionadded:: 3.8

Sets the local debugger working directory for Visual Studio C++ targets.
The property value may use
:manual:`generator expressions <cmake-generator-expressions(7)>`.
This is defined in ``<LocalDebuggerWorkingDirectory>`` in the Visual Studio
project file.

This property only works for Visual Studio 2010 and above;
it is ignored on other generators.
