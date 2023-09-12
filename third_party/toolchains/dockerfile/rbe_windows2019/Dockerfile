# This dockerfile is taken from go/rbe-windows-user-guide
# Local modifications:
# * install VS2019 (instead of VS2022)
# TODO(jtattermusch): check the --compilation_mode=dbg fix

# This Dockerfile creates an image that has the following:
#  * correct MTU setting for networking from inside the container to work.
#  * metadata server routes correctly installed 
#  * VC++ redistributable installed
#  * Visual Studio 2019 Build Tools installed
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

# Install Visual Studio 2019 Build Tools.
RUN Invoke-WebRequest "https://aka.ms/vs/16/release/vs_buildtools.exe" \
        -OutFile C:/TEMP/vs_buildtools.exe -UseBasicParsing; \
    Start-Process -FilePath C:/TEMP/vs_buildtools.exe -ArgumentList "--installPath", "C:/VS", \
        "--quiet", "--wait", "--nocache", \
        "--add", "Microsoft.VisualStudio.Workload.VCTools", \
        "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", \
        "--add", "Microsoft.VisualStudio.Component.Windows10SDK.20348" -Wait; \
    Remove-Item C:/TEMP/vs_buildtools.exe; \
    [Environment]::SetEnvironmentVariable(\"BAZEL_VC\", \"C:\VS\VC\", \"Machine\")
# Install msys2 and add to path.
RUN [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; \
    Invoke-WebRequest "https://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20220319.sfx.exe" \
        -OutFile msys2_install.exe -UseBasicParsing; \
    .\msys2_install.exe -y -oC:\; \
    Remove-Item msys2_install.exe; \
    function msys() { C:\msys64\usr\bin\bash.exe @('-lc') + @Args; } \
    msys ' '; \
    msys 'pacman --noconfirm -Syy zstd'; \
    msys 'pacman --noconfirm -Syy git curl zip unzip'; \
    $old_path = [Environment]::GetEnvironmentVariable(\"PATH\", \"Machine\"); \
    [Environment]::SetEnvironmentVariable(\"PATH\", $old_path + \";C:\msys64;C:\msys64\usr\bin\", \"Machine\");
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
# Restore default shell for Windows containers.
SHELL ["cmd.exe", "/s", "/c"]
# Default to PowerShell if no other command specified.
CMD ["powershell.exe", "-NoLogo", "-ExecutionPolicy", "Bypass"]
