# This dockerfile is taken from go/rbe-windows-user-guide

# This Dockerfile creates an image that has the following:
#  * correct MTU setting for networking from inside the container to work.
#  * metadata server routes correctly installed
#  * VC++ redistributable installed
#  * Visual Studio 2022 Build Tools installed
#  * msys2 + git, curl, zip, unzip installed
#  * Python 3.10.4 installed
#  * JDK 17.0.2 installed
# Use the latest stable Windows Server Core image (mainstream support ends 1/9/2024).
FROM mcr.microsoft.com/windows/servercore:ltsc2019
# Set default powershell policy for this script (ProgressPreference='SilentlyContinue' makes
# downloads with Invoke-WebRequest not show the progress bar and is MUCH faster).
SHELL ["powershell.exe", "-ExecutionPolicy", "Bypass", "-Command", "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; $VerbosePreference = 'Continue';"]
# Workaround for networking (b/112379377) was closed as won't fix for MTU setting.
# Remaining lines handle making the metadata server on the VM accessible inside docker.
RUN Get-NetAdapter | Where-Object Name -like "*Ethernet*" | ForEach-Object { \
        & netsh interface ipv4 set subinterface $_.InterfaceIndex mtu=1460 store=persistent }; \
    $gateway = (Get-NetRoute | Where { $_.DestinationPrefix -eq \"0.0.0.0/0\" } | Sort-Object RouteMetric \
        | Select NextHop).NextHop; \
    $ifIndex = (Get-NetAdapter -InterfaceDescription \"Hyper-V Virtual Ethernet*\" | Sort-Object \
        | Select ifIndex).ifIndex; \
    New-NetRoute -DestinationPrefix 169.254.169.254/32 -InterfaceIndex $ifIndex -NextHop $gateway
# Enable Long Paths for Win32 File/Folder APIs.
RUN New-ItemProperty -Path HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem \
        -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force
# Install Visual C++ Redistributable for Visual Studio 2015-2022.
RUN New-Item -Path "C:/" -Name "TEMP" -ItemType "directory"; \
    Invoke-WebRequest "https://aka.ms/vs/17/release/vc_redist.x64.exe" \
        -OutFile C:/TEMP/vc_redist.x64.exe -UseBasicParsing; \
    Start-Process -filepath C:/TEMP/vc_redist.x64.exe -ArgumentList '/install', '/passive', '/norestart' -Wait; \
    Remove-Item C:/TEMP/vc_redist.x64.exe
# Install Visual Studio 2022 Build Tools.
# Note: If bazel 4.x or under are needed, you'll need to downgrade this to VS 2019 by changing the
#       "17" in the URL on the next line to "16".
RUN Invoke-WebRequest "https://aka.ms/vs/17/release/vs_buildtools.exe" \
        -OutFile C:/TEMP/vs_buildtools.exe -UseBasicParsing; \
    Start-Process -FilePath C:/TEMP/vs_buildtools.exe -ArgumentList \
        "--quiet", "--wait", "--nocache", \
        "--add", "Microsoft.VisualStudio.Workload.VCTools", \
        "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", \
        "--add", "Microsoft.VisualStudio.Component.Windows10SDK.20348" -Wait; \
    Remove-Item C:/TEMP/vs_buildtools.exe; \
    [Environment]::SetEnvironmentVariable(\"BAZEL_VC\", \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\", \"Machine\")
# Install msys2 and add to path.
RUN [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; \
    Invoke-WebRequest "https://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20240507.sfx.exe" \
        -OutFile msys2_install.exe -UseBasicParsing; \
    .\msys2_install.exe -y -oC:\tools; \
    Remove-Item msys2_install.exe; \
    function msys() { C:\tools\msys64\usr\bin\bash.exe @('-lc') + @Args; } \
    msys ' '; \
    msys 'pacman --noconfirm -Syy zstd'; \
    msys 'pacman --noconfirm -Syy git curl zip unzip'; \
    $old_path = [Environment]::GetEnvironmentVariable(\"PATH\", \"Machine\"); \
    [Environment]::SetEnvironmentVariable(\"PATH\", $old_path + \";C:\tools\msys64;C:\tools\msys64\usr\bin\", \"Machine\");
# Install Python 3.
RUN Invoke-WebRequest "https://www.python.org/ftp/python/3.10.4/python-3.10.4-amd64.exe" \
                -OutFile C:/TEMP/python_install.exe -UseBasicParsing; \
    Start-Process C:/TEMP/python_install.exe -ArgumentList "/quiet", "/log", "C:/TEMP/python_install_log.txt", \
        "InstallAllUsers=1", "PrependPath=1" -wait; \
    Remove-Item C:/TEMP/python_install.exe; \
    Remove-Item C:/TEMP/python_install_log.txt
# Install JDK 17
RUN Add-Type -AssemblyName "System.IO.Compression.FileSystem"; \
    $zulu_url = \"https://cdn.azul.com/zulu/bin/zulu17.32.13-ca-jdk17.0.2-win_x64.zip\"; \
    $zulu_zip = \"c:/temp/jdk_install.zip\"; \
    $zulu_extracted_path = \"c:/temp/\" + [IO.Path]::GetFileNameWithoutExtension($zulu_url); \
    $zulu_root = \"c:/openjdk\"; \
    (New-Object Net.WebClient).DownloadFile($zulu_url, $zulu_zip); \
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zulu_zip, \"c:/temp\"); \
    Move-Item $zulu_extracted_path -Destination $zulu_root; \
    Remove-Item $zulu_zip; \
    $old_path = [Environment]::GetEnvironmentVariable(\"PATH\", \"Machine\"); \
    [Environment]::SetEnvironmentVariable(\"PATH\", $old_path + \";${zulu_root}\bin\", \"Machine\"); \
    [Environment]::SetEnvironmentVariable(\"JAVA_HOME\", $zulu_root, \"Machine\")
# Install gcloud (install.bat installs directly into bin folder of extracted zip contents)
RUN Add-Type -AssemblyName "System.IO.Compression.FileSystem"; \
    $pkg_url = \"https://dl.google.com/dl/cloudsdk/channels/rapid/downloads/google-cloud-cli-396.0.0-windows-x86_64.zip\"; \
    $pkg_zip = \"c:/temp/gcloud.zip\"; \
    $pkg_extracted_path = \"c:/google-cloud-sdk\"; \
    (New-Object Net.WebClient).DownloadFile($pkg_url, $pkg_zip); \
    [System.IO.Compression.ZipFile]::ExtractToDirectory($pkg_zip, \"c:/\"); \
    Start-Process cmd.exe -ArgumentList "/c", "/s", "$pkg_extracted_path/install.bat", "-q" -wait; \
    Remove-Item $pkg_zip; \
    $old_path = [Environment]::GetEnvironmentVariable(\"PATH\", \"Machine\"); \
    [Environment]::SetEnvironmentVariable(\"PATH\", $old_path + \";${pkg_extracted_path}\bin\", \"Machine\")
# Set up docker client in the container (required to test Docker sibling containers).
# PATH isn't actually set in the Docker image, so we have to first set it from within the container.
RUN $docker_path = ('{0}\docker' -f $env:ProgramFiles); \
    $old_path = [Environment]::GetEnvironmentVariable(\"PATH\", \"Machine\"); \
    [Environment]::SetEnvironmentVariable(\"PATH\", $old_path + \";${docker_path}\", \"Machine\")
# Download and install the same docker version used in the VM image.
ENV DOCKER_VERSION 20.10.23
ENV DOCKER_URL https://download.docker.com/win/static/stable/x86_64/docker-20.10.23.zip
RUN Write-Host ('Downloading {0} ...' -f $env:DOCKER_URL); \
        Invoke-WebRequest -Uri $env:DOCKER_URL -OutFile 'docker.zip'; \
        Write-Host 'Expanding ...'; \
        Expand-Archive docker.zip -DestinationPath $env:ProgramFiles; \
# (this archive has a "docker/..." directory in it already).
        Write-Host 'Removing ...'; \
        Remove-Item @( \
                        'docker.zip', \
                        ('{0}\docker\dockerd.exe' -f $env:ProgramFiles) \
                ) -Force; \
        Write-Host 'Verifying install ("docker --version") ...'; \
        docker --version; \
        Write-Host 'Complete.';
# Restore default shell for Windows containers.
SHELL ["cmd.exe", "/s", "/c"]
# Default to PowerShell if no other command specified.
CMD ["powershell.exe", "-NoLogo", "-ExecutionPolicy", "Bypass"]
