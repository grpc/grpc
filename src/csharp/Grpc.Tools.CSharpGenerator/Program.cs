using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

namespace Grpc.Tools.CSharpGenerator
{
    class Program
    {
        static void Main(string[] args)
        {
            try
            {
                var projectDir = args[0];
                var intermediateOutputDir = args[1];

                var osName = string.Empty;

                if (Grpc.Core.Internal.PlatformApis.IsWindows)
                    osName = "windows_{0}";
                else if (Grpc.Core.Internal.PlatformApis.IsMacOSX)
                    osName = "macosx_{0}";
                else if (Grpc.Core.Internal.PlatformApis.IsLinux)
                    osName = "linux_{0}";
                else
                {
                    Console.Error.WriteLine("Could not detect your operating system. The only supported operating systems are Windows, Mac OSX and Linux");
                    return;
                }

                var bitness = string.Empty;
                if (Grpc.Core.Internal.PlatformApis.Is64Bit)
                    bitness = "x64";
                else if (IntPtr.Size == 4) // We don't want to assume all other cases mean we're x86
                    bitness = "x86";
                else
                {
                    Console.Error.WriteLine("Could not detect your the architecture of your operating system. Only x86 and x64 are currently supported");
                    return;
                }

                var exeDir = Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);
                var protocPath = Path.Combine(exeDir.ParentDirectory(), "tools", string.Format(osName, bitness), osName.StartsWith("windows") ? "protoc.exe" : "protoc");
                var grpcCSharpPluginPath = Path.Combine(exeDir.ParentDirectory(), "tools", string.Format(osName, bitness), osName.StartsWith("windows") ? "grpc_csharp_plugin.exe" : "grpc_csharp_plugin");
                var protosIncludeDir = projectDir;
                var outputDir = Path.Combine(projectDir, intermediateOutputDir);

                var inputFiles = from protofile in args.Skip(2).TakeWhile(s => true)
                                 select Path.Combine(projectDir, protofile);
                foreach (var f in inputFiles)
                    Console.WriteLine("Going to process: {0}", f);

                foreach (var inputFile in inputFiles)
                {
                    var protocArguments = string.Format("-I{0} --csharp_out {1} --grpc_out {1} --plugin=protoc-gen-grpc={2} {3}", protosIncludeDir, outputDir, grpcCSharpPluginPath, inputFile);
                    Console.WriteLine("Generating Grpc for {0}...", inputFile);
                    Console.WriteLine("Command line: {0} {1}", protocPath, protocArguments);
                    var process = Process.Start(new ProcessStartInfo
                        {
                            FileName = protocPath,
                            Arguments = protocArguments,
                            WindowStyle = ProcessWindowStyle.Hidden,
                            UseShellExecute = false
                        });
                    process.WaitForExit();
                    if (process.ExitCode != 0)
                        Console.Error.WriteLine("The process {0} with commandline {1} {2} exited with code {3}", process.Id, protocPath, protocArguments, process.ExitCode);
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Exception: \"{0}\". Stacktrace follows.\n{1}", ex.Message, ex.StackTrace);
            }
        }
    }

    internal static class DirectoryExtensions
    {
        public static string ParentDirectory(this string directory)
        {
            return Directory.GetParent(directory).FullName;
        }
    }
}
