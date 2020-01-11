# This Dockerfile creates an image that:
# - Has the correct MTU setting for networking from inside the container to work.
# - Has Visual Studio 2015 Build Tools installed.
# - Has msys2 + git, curl, zip, unzip installed.
# - Has Python 2.7 installed.
# - Has Bazel installed.

# TODO(jsharpe): Consider replacing "ADD $URI $DEST" with "Invoke-WebRequest -Method Get -Uri $URI -OutFile $DEST"

# Use the latest Windows Server Core image.
#
# WARNING: What's the `:1803` about? There are two versions of Windows Server
# 2016: a "regular" one (corresponding to `microsoft/windowsservercore`) is on
# a slow release cadence, and a fast release cadence one (corresponding to
# `microsoft/windowsservercore:1803`). If you chose a different image than
# described above, probably omit the `:1803` or change it to a different
# number.
FROM microsoft/windowsservercore:1803

SHELL ["powershell.exe", "-ExecutionPolicy", "Bypass", "-Command", "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; $VerbosePreference = 'Continue';"]

# TODO(b/112379377): Workaround until bug is fixed.
RUN netsh interface ipv4 set subinterface \"vEthernet (Ethernet)\" mtu=1460 store=persistent

# Install Visual Studio 2015 Build Tools.
RUN Invoke-WebRequest "https://download.microsoft.com/download/5/f/7/5f7acaeb-8363-451f-9425-68a90f98b238/visualcppbuildtools_full.exe" \
                -OutFile visualcppbuildtools_full.exe -UseBasicParsing ; \
        Start-Process -FilePath 'visualcppbuildtools_full.exe' -ArgumentList '/quiet', '/NoRestart' -Wait ; \
        Remove-Item .\visualcppbuildtools_full.exe;

# TODO(jsharpe): Alternate install for msys2: https://github.com/StefanScherer/dockerfiles-windows/issues/30

# Install 7-Zip and add it to the path.
ADD https://www.7-zip.org/a/7z1801-x64.msi C:\\TEMP\\7z.msi
RUN Start-Process msiexec.exe -ArgumentList \"/i C:\\TEMP\\7z.msi /qn /norestart /log C:\\TEMP\\7z_install_log.txt\" -wait
RUN $oldpath = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH).path; \
  $newpath = \"$oldpath;C:\Program Files\7-Zip\"; \
  Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH -Value $newPath

# Install msys2, and add some extra tools.
ADD http://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20161025.tar.xz C:\\TEMP\\msys2.tar.xz
RUN 7z x C:\TEMP\msys2.tar.xz -oC:\TEMP\msys2.tar
RUN 7z x C:\TEMP\msys2.tar -oC:\tools
RUN $oldpath = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH).path; \
  $newpath = \"$oldpath;C:\tools\msys64;C:\tools\msys64\usr\bin\"; \
  Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH -Value $newPath
RUN Start-Process msys2 -ArgumentList 'pacman -noconfirm -Syuu git curl zip unzip' -Wait

# Install Visual C++ Redistributable for Visual Studio 2015:
ADD https://download.microsoft.com/download/9/3/F/93FCF1E7-E6A4-478B-96E7-D4B285925B00/vc_redist.x64.exe C:\\TEMP\\vc_redist.x64.exe
RUN C:\TEMP\vc_redist.x64.exe /quiet /install

# Install Python 2.7.
ADD https://www.python.org/ftp/python/2.7.14/python-2.7.14.amd64.msi C:\\TEMP\\python.msi
RUN Start-Process msiexec.exe -ArgumentList \"/i C:\\TEMP\\python.msi /qn /norestart /log C:\\TEMP\\python_install_log.txt\" -wait
RUN $oldpath = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH).path; \
  $newpath = \"$oldpath;C:\Python27\"; \
  Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH -Value $newPath

# Install Bazel.
RUN Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name BAZEL_SH -Value \"C:\tools\msys64\usr\bin\bash.exe\"
RUN Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name BAZEL_VC -Value \"C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\"
RUN [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12;
ADD https://github.com/bazelbuild/bazel/releases/download/0.26.0/bazel-0.26.0-windows-x86_64.exe C:\\bin\\bazel.exe
RUN $oldpath = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH).path; \
  $newpath = \"$oldpath;C:\bin\"; \
  Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH -Value $newPath

# TODO(jsharpe): This requires entropy so may be problematic on a headless machine: https://wiki.archlinux.org/index.php/Pacman/Package_signing#Initializing_the_keyring
RUN Start-Process msys2 -ArgumentList 'pacman-key --init' -Wait
# TODO(jsharpe): If you don't run this then the next command can't succeed since it needs to prompt to remove catgets.
RUN pacman --noconfirm -R libcatgets catgets
# Bazel documentation says to use -Syuu but this doesn't work in Docker. See
# http://g/foundry-windows/PDMVXbGew7Y
RUN pacman --noconfirm -Syy git curl zip unzip

RUN \
  Add-Type -AssemblyName \"System.IO.Compression.FileSystem\"; \
  $zulu_url = \"https://cdn.azul.com/zulu/bin/zulu8.28.0.1-jdk8.0.163-win_x64.zip\"; \
  $zulu_zip = \"c:\\temp\\zulu8.28.0.1-jdk8.0.163-win_x64.zip\"; \
  $zulu_extracted_path = \"c:\\temp\\\" + [IO.Path]::GetFileNameWithoutExtension($zulu_zip); \
  $zulu_root = \"c:\\openjdk\"; \
  (New-Object Net.WebClient).DownloadFile($zulu_url, $zulu_zip); \
  [System.IO.Compression.ZipFile]::ExtractToDirectory($zulu_zip, \"c:\\temp\"); \
  Move-Item $zulu_extracted_path -Destination $zulu_root; \
  Remove-Item $zulu_zip; \
  $env:PATH = [Environment]::GetEnvironmentVariable(\"PATH\", \"Machine\") + \";${zulu_root}\\bin\"; \
  [Environment]::SetEnvironmentVariable(\"PATH\", $env:PATH, \"Machine\"); \
  $env:JAVA_HOME = $zulu_root; \
  [Environment]::SetEnvironmentVariable(\"JAVA_HOME\", $env:JAVA_HOME, \"Machine\")

# Restore default shell for Windows containers.
SHELL ["cmd.exe", "/s", "/c"]

# Default to PowerShell if no other command specified.
CMD ["powershell.exe", "-NoLogo", "-ExecutionPolicy", "Bypass"]
