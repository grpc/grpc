FOLDER
------

Set the folder name. Use to organize targets in an IDE.

Targets with no ``FOLDER`` property will appear as top level entities in
IDEs like Visual Studio.  Targets with the same ``FOLDER`` property value
will appear next to each other in a folder of that name.  To nest
folders, use ``FOLDER`` values such as 'GUI/Dialogs' with '/' characters
separating folder levels.

This property is initialized by the value of the variable
:variable:`CMAKE_FOLDER` if it is set when a target is created.
