using System.Reflection;
using System.Runtime.CompilerServices;

[assembly: AssemblyTitle("Grpc.Core")]
[assembly: AssemblyDescription("")]
[assembly: AssemblyConfiguration("")]
[assembly: AssemblyCompany("")]
[assembly: AssemblyProduct("")]
[assembly: AssemblyCopyright("Google Inc.  All rights reserved.")]
[assembly: AssemblyTrademark("")]
[assembly: AssemblyCulture("")]

#if SIGNED
[assembly: InternalsVisibleTo("Grpc.Core.Tests,PublicKey=" +
    "00240000048000009400000006020000002400005253413100040000010001002f5797a92c6fcde81bd4098f43" +
    "0442bb8e12768722de0b0cb1b15e955b32a11352740ee59f2c94c48edc8e177d1052536b8ac651bce11ce5da3a" +
    "27fc95aff3dc604a6971417453f9483c7b5e836756d5b271bf8f2403fe186e31956148c03d804487cf642f8cc0" +
    "71394ee9672dfe5b55ea0f95dfd5a7f77d22c962ccf51320d3")]
[assembly: InternalsVisibleTo("Grpc.IntegrationTesting,PublicKey=" +
    "00240000048000009400000006020000002400005253413100040000010001002f5797a92c6fcde81bd4098f43" +
    "0442bb8e12768722de0b0cb1b15e955b32a11352740ee59f2c94c48edc8e177d1052536b8ac651bce11ce5da3a" +
    "27fc95aff3dc604a6971417453f9483c7b5e836756d5b271bf8f2403fe186e31956148c03d804487cf642f8cc0" +
    "71394ee9672dfe5b55ea0f95dfd5a7f77d22c962ccf51320d3")]
#else
[assembly: InternalsVisibleTo("Grpc.Core.Tests")]
[assembly: InternalsVisibleTo("Grpc.IntegrationTesting")]
#endif
