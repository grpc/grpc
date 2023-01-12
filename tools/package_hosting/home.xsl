<?xml version="1.0"?>
<xsl:stylesheet version="2.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:template match="//packages">
  <html>
    <head>
      <title>gRPC Packages</title>
      <link rel="stylesheet" type="text/css" href="/web-assets/style.css" />
      <link rel="apple-touch-icon" href="/web-assets/favicons/apple-touch-icon.png" sizes="180x180" />
      <link rel="icon" type="image/png" href="/web-assets/favicons/android-chrome-192x192.png" sizes="192x192" />
      <link rel="icon" type="image/png" href="/web-assets/favicons/favicon-32x32.png" sizes="32x32" />
      <link rel="icon" type="image/png" href="/web-assets/favicons/favicon-16x16.png" sizes="16x16" />
      <link rel="manifest" href="/web-assets/favicons/manifest.json" />
      <link rel="mask-icon" href="/web-assets/favicons/safari-pinned-tab.svg" color="#2DA6B0" />
      <meta name="msapplication-TileColor" content="#ffffff" />
      <meta name="msapplication-TileImage" content="/web-assets/favicons/mstile-150x150.png" />
      <meta name="og:title" content="gRPC Packages"/>
      <meta name="og:image" content="https://grpc.io/img/grpc_square_reverse_4x.png"/>
      <meta name="og:description" content="gRPC Packages"/>
    </head>
    <body bgcolor="#ffffff">
      <div id="topbar">
        <span class="title">gRPC Packages</span>
      </div>
      <div id="main">
        <xsl:apply-templates select="releases" />
        <xsl:apply-templates select="builds" />
        <br />
        <br />
        <p class="description"><a href="https://grpc.io">gRPC</a> is a <a href="https://www.cncf.io" class="external">Cloud Native Computing Foundation</a> project. <a href="https://policies.google.com/privacy" class="external">Privacy Policy</a>.</p>
        <p class="description">Copyright &#169; 2018 <a href="https://github.com/grpc/grpc/blob/master/AUTHORS">The gRPC Authors</a></p>
      </div>
    </body>
  </html>
</xsl:template>

<xsl:template match="releases">
  <h2>Official gRPC Releases</h2>
  <p>Commits corresponding to  <a href="https://github.com/grpc/grpc/releases">official gRPC release points and release candidates</a> are tagged on GitHub.</p>
  <p>To maximize usability, gRPC supports the standard way of adding dependencies in your language of choice (if there is one).
  In most languages, the gRPC runtime comes in form of a package available in your language's package manager.</p>
  <p>For instructions on how to use the language-specific gRPC runtime in your project, please refer to the following:</p>
  <ul>
    <li><a href="https://github.com/grpc/grpc/blob/master/src/cpp">C++</a>: follow the instructions under the <a href="https://github.com/grpc/grpc/tree/master/src/cpp"><code>src/cpp</code> directory</a></li>
    <li><a href="https://github.com/grpc/grpc/blob/master/src/csharp">C#</a>: NuGet package <code>Grpc</code></li>
    <li><a href="https://github.com/grpc/grpc-dart">Dart</a>: pub package <code>grpc</code></li>
    <li><a href="https://github.com/grpc/grpc-go">Go</a>: <code>go get google.golang.org/grpc</code></li>
    <li><a href="https://github.com/grpc/grpc-java">Java</a>: Use JARs from <a href="https://search.maven.org/search?q=g:io.grpc">gRPC Maven Central Repository</a></li>
    <li><a href="https://github.com/grpc/grpc-kotlin">Kotlin</a>: Use JARs from <a href="https://mvnrepository.com/artifact/io.grpc">gRPC Maven Central Repository</a></li>
    <li><a href="https://github.com/grpc/grpc-node">Node</a>: <code>npm install @grpc/grpc-js</code></li>
    <li><a href="https://github.com/grpc/grpc/blob/master/src/objective-c">Objective-C</a>: Add <code>gRPC-ProtoRPC</code> dependency to podspec</li>
    <li><a href="https://github.com/grpc/grpc/blob/master/src/php">PHP</a>: <code>pecl install grpc</code></li>
    <li><a href="https://github.com/grpc/grpc/blob/master/src/python/grpcio">Python</a>: <code>pip install grpcio</code></li>
    <li><a href="https://github.com/grpc/grpc/blob/master/src/ruby">Ruby</a>: <code>gem install grpc</code></li>
    <li><a href="https://github.com/grpc/grpc-web">WebJS</a>: follow the <a href="https://github.com/grpc/grpc-web">instructions in <code>grpc-web</code> repository</a></li>
  </ul>
</xsl:template>

<xsl:template match="builds">
  <h2> Daily Builds of <a href="https://github.com/grpc/grpc/tree/master"><code>master</code></a> Branch</h2>
  <p>gRPC packages are built on a daily basis at the <code>HEAD</code> of <a href="https://github.com/grpc/grpc/tree/master">the <code>master</code> branch</a> and are archived here.</p>
  <p>
    <a href="#">The current document</a> (view source) is an XML feed pointing to the packages as they get built and uploaded.
    You can subscribe to this feed and fetch, deploy, and test the precompiled packages with your continuous integration infrastructure.
  </p>
  <p>For stable release packages, please consult the above section and the common package manager for your language.</p>
  <table style="border:solid black 1px">
    <tr style="background-color:lightgray">
      <td>Timestamp</td>
      <td>Commit</td>
      <td>Build ID</td>
    </tr>
    <xsl:apply-templates select="build[@branch='master']">
      <xsl:sort select="@timestamp" data-type="text" order="descending" />
    </xsl:apply-templates>
  </table>
</xsl:template>

<xsl:template match="build">
  <tr>
    <td class="name"><xsl:value-of select="@timestamp" /></td>
    <td class="name"> <a href="https://github.com/grpc/grpc/tree/{@commit}"><xsl:value-of select="@commit" /></a></td>
    <td class="name"> <a href="{@path}"><xsl:value-of select="@id" /></a></td>
  </tr>
</xsl:template>

</xsl:stylesheet>
