# This dockerfile is taken from go/rbe-windows-user-guide
# (including the fix --compilation_mode=dbg)

# This Dockerfile creates an image that:
# - Has the correct MTU setting for networking from inside the container to work.
# - Has Visual Studio 2015 Build Tools installed.
# - Has msys2 + git, curl, zip, unzip installed.
# - Has Python 2.7 installed.
# TODO(jsharpe): Consider replacing "ADD $URI $DEST" with "Invoke-WebRequest -Method Get -Uri $URI -OutFile $DEST"
# Use the latest Windows Server Core image.
#
# WARNING: What's the `:ltsc2019` about?
# There are two versions of Windows Server 2019:
# 1. A "regular" one (corresponding to `mcr.microsoft.com/windows/servercore:ltsc2019`)
# is on a slow release cadence and is the Long-Term Servicing Channel.
# Mainstream support for this image will end on 1/9/2024.
# 2. A "fast" release cadence one (corresponding to
# `mcr.microsoft.com/windows/servercore:1909`) is the Semi-Annual Channel.
# Mainstream support for this image will end on 5/11/2021.
#
# If you choose a different
# image than described above, change the `:ltsc2019` tag.
# Start a temporary container in which we install 7-Zip to extract msys2
FROM mcr.microsoft.com/windows/servercore:ltsc2019 as extract-msys2
SHELL ["powershell.exe", "-ExecutionPolicy", "Bypass", "-Command", "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; $VerbosePreference = 'Continue';"]
# Install 7-Zip and add it to the path.
ADD https://www.7-zip.org/a/7z1801-x64.msi C:\\TEMP\\7z.msi
RUN Start-Process msiexec.exe -ArgumentList \"/i C:\\TEMP\\7z.msi /qn /norestart /log C:\\TEMP\\7z_install_log.txt\" -wait
RUN $oldpath = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH).path; \
  $newpath = \"$oldpath;C:\Program Files\7-Zip\"; \
  Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH -Value $newPath
# Extract msys2
ADD http://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20181211.tar.xz C:\\TEMP\\msys2.tar.xz
RUN 7z x C:\TEMP\msys2.tar.xz -oC:\TEMP\msys2.tar
RUN 7z x C:\TEMP\msys2.tar -oC:\tools
# Start building the actual image
FROM mcr.microsoft.com/windows/servercore:ltsc2019
SHELL ["powershell.exe", "-ExecutionPolicy", "Bypass", "-Command", "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; $VerbosePreference = 'Continue';"]
# TODO(b/112379377): Workaround until bug is fixed.
RUN Get-NetAdapter | Where-Object Name -like "*Ethernet*" | ForEach-Object { & netsh interface ipv4 set subinterface $_.InterfaceIndex mtu=1460 store=persistent }
# Enable Long Paths for Win32 File/Folder APIs.
RUN New-ItemProperty -Path HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force

# Install Visual Studio 2015 Build Tools.
RUN Invoke-WebRequest "https://download.microsoft.com/download/5/f/7/5f7acaeb-8363-451f-9425-68a90f98b238/visualcppbuildtools_full.exe" \
                -OutFile visualcppbuildtools_full.exe -UseBasicParsing ; \
        Start-Process -FilePath 'visualcppbuildtools_full.exe' -ArgumentList '/quiet', '/NoRestart', '/InstallSelectableItems "Win10SDK_VisibleV1"' -Wait ; \
        Remove-Item .\visualcppbuildtools_full.exe;
# Add ucrtbased.dll to the system directory to allow --compilation_mode=dbg to
# work. This DLL should be automatically copied to C:\Windows\System32 by the
# installer, but isn't when the installer is run on Docker, for some reason.
RUN Copy-Item \"C:\Program Files (x86)\Windows Kits\10\bin\x64\ucrt\ucrtbased.dll\" C:\Windows\System32


# TODO(jsharpe): Alternate install for msys2: https://github.com/StefanScherer/dockerfiles-windows/issues/30
# From the temporary extract-msys2 container, copy the tools directory to this container
COPY --from=extract-msys2 ["C:/tools", "C:/tools"]
# Add msys2 to the PATH variable
RUN $oldpath = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH).path; \
  $newpath = \"$oldpath;C:\tools\msys64;C:\tools\msys64\usr\bin\"; \
  Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH -Value $newPath
# Bazel documentation says to use -Syuu but this doesn't work in Docker. See
# http://g/foundry-windows/PDMVXbGew7Y
RUN bash.exe -c \"pacman-key --init && pacman-key --populate msys2 && pacman-key --refresh-keys && pacman --noconfirm -Syy git curl zip unzip\"
# Install Visual C++ Redistributable for Visual Studio 2015:
ADD https://download.microsoft.com/download/9/3/F/93FCF1E7-E6A4-478B-96E7-D4B285925B00/vc_redist.x64.exe C:\\TEMP\\vc_redist.x64.exe
RUN C:\TEMP\vc_redist.x64.exe /quiet /install
# Install Python 2.7.
ADD https://www.python.org/ftp/python/2.7.14/python-2.7.14.amd64.msi C:\\TEMP\\python.msi
RUN Start-Process msiexec.exe -ArgumentList \"/i C:\\TEMP\\python.msi /qn /norestart /log C:\\TEMP\\python_install_log.txt\" -wait
RUN $oldpath = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH).path; \
  $newpath = \"$oldpath;C:\Python27\"; \
  Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH -Value $newPath
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



