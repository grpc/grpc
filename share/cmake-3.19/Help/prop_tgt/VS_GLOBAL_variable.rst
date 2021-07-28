VS_GLOBAL_<variable>
--------------------

Visual Studio project-specific global variable.

Tell the Visual Studio generator to set the global variable
'<variable>' to a given value in the generated Visual Studio project.
Ignored on other generators.  Qt integration works better if
VS_GLOBAL_QtVersion is set to the version FindQt4.cmake found.  For
example, "4.7.3"
