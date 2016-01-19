@rem Builds C# artifacts on Windows

@call vsprojects\build_vs2013.bat %* || goto :error

mkdir artifacts
copy /Y vsprojects\Release\grpc_csharp_ext.dll artifacts || copy /Y vsprojects\x64\Release\grpc_csharp_ext.dll artifacts || goto :error

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
