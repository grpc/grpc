using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace Grpc.Tools.CSharpGenerator
{
    class Program
    {
        static readonly Regex _dependenciesRegex = new Regex(@"[^\s]*\.proto", RegexOptions.Compiled);
        static readonly Regex _generatedFilesRegex = new Regex(@"[^\s]*\.cs", RegexOptions.Compiled);

        static bool ProcessFile(string protocPath, string protosIncludeDir, string projectDir, string outputDir, string grpcCSharpPluginPath, string inputFile, 
            ref IEnumerable<string> outputFiles)
        {
            try
            {
                var protocArguments = string.Format("-I{0} --csharp_out {1} --grpc_out {1} --plugin=protoc-gen-grpc={2} --dependency_out={3}.deps {4}",
                    protosIncludeDir, Path.Combine(outputDir, Path.GetDirectoryName(inputFile)), grpcCSharpPluginPath, Path.Combine(outputDir, inputFile), inputFile);
                Console.WriteLine("Generating Grpc for {0}...", inputFile);
                Console.WriteLine("Command line: {0} {1}", protocPath, protocArguments);
                var process = Process.Start(new ProcessStartInfo
                {
                    FileName = protocPath,
                    Arguments = protocArguments,
                    WindowStyle = ProcessWindowStyle.Hidden,
                    WorkingDirectory = projectDir,
                    UseShellExecute = false
                });
                process.WaitForExit();
                if (process.ExitCode != 0)
                {
                    Console.Error.WriteLine("The process {0} with commandline {1} {2} exited with code {3}", process.Id, protocPath, protocArguments, process.ExitCode);
                    return false;
                }

                // Process dependencies
                var depsFilename = string.Format("{0}.deps", Path.Combine(outputDir, inputFile));
                var depsText = File.ReadAllText(depsFilename);
                Console.WriteLine("{0}.deps\n{1}\n", Path.Combine(outputDir, inputFile), depsText);
                var outputsMatch = _generatedFilesRegex.Matches(depsText);
                var depsMatch = _dependenciesRegex.Matches(depsText);

                var inputFileFullPath = Path.GetFullPath(inputFile);

                // Process all files except the one we just processed now
                var dependencies = from dep in depsMatch.AsEnumerable()
                                   let depFullPath = Path.GetFullPath(dep.Value)
                                   where string.Compare(inputFileFullPath, depFullPath,
                                   Path.DirectorySeparatorChar == '\\' ? StringComparison.CurrentCultureIgnoreCase : StringComparison.CurrentCulture) != 0
                                   select dep.Value;
                var generatedFiles = from o in outputsMatch.AsEnumerable()
                                     let outputPath = Path.GetFullPath(o.Value)
                                     select o.Value;

                foreach (var f in generatedFiles)
                    Console.WriteLine("Generated: {0}", f);
                
                // Add all generated files to outputFiles
                outputFiles = outputFiles.Union(generatedFiles);
                outputFiles = outputFiles.Union(new[] { depsFilename });

                foreach (var dep in dependencies)
                    if (!ProcessFile(protocPath, protosIncludeDir, projectDir, outputDir, grpcCSharpPluginPath, dep, ref outputFiles))
                        return false;
                return true;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Exception: \"{0}\". Stacktrace follows.\n{1}", ex.Message, ex.StackTrace);
                return false;
            }
        }

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
                var protosIncludeDir = ".";
                var outputDir = Path.Combine(projectDir, intermediateOutputDir);

                var inputFiles = args.Skip(2).TakeWhile(s => true);
                Console.WriteLine("Input files: {0}", string.Join(", ", inputFiles));

                var outputFiles = Enumerable.Empty<string>();
                foreach (var inputFile in inputFiles)
                {
                    Console.WriteLine("Going to process: {0}", inputFile);
                    if (!ProcessFile(protocPath, protosIncludeDir, projectDir, outputDir, grpcCSharpPluginPath, inputFile, ref outputFiles))
                    {
                        Console.Error.WriteLine("Could not process {0}", inputFile);
                        break;
                    }
                }

                var protocsPath = Path.Combine(outputDir, "protocs.txt");
                var protodepsPath = Path.Combine(outputDir, "protodeps.txt");
                outputFiles = outputFiles.Union(new[] { protocsPath });

                var nonStandardDirSeparator = Grpc.Core.Internal.PlatformApis.IsWindows ? '/' : '\\';

                // We're done, write out all the output file paths
                // CS files (for compile, clean)
                File.WriteAllText(protocsPath, string.Join("\n", from o in outputFiles where o.EndsWith(".cs") select o.Replace(nonStandardDirSeparator, Path.DirectorySeparatorChar)));
                // All other files (for clean)
                File.WriteAllText(protodepsPath, string.Join("\n", from o in outputFiles where !o.EndsWith(".cs") select o.Replace(nonStandardDirSeparator, Path.DirectorySeparatorChar)));
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Exception: \"{0}\". Stacktrace follows.\n{1}", ex.Message, ex.StackTrace);
            }
        }
    }

    internal static class Extensions
    {
        public static string ParentDirectory(this string directory)
        {
            return Directory.GetParent(directory).FullName;
        }

        public static IEnumerable<Capture> AsEnumerable(this CaptureCollection collection)
        {
            for (int i = 0; i < collection.Count; i++)
                yield return collection[i];
        }
        public static IEnumerable<Match> AsEnumerable(this MatchCollection collection)
        {
            for (int i = 0; i < collection.Count; i++)
                yield return collection[i];
        }
    }
}
