<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:template match="//build">
<html>
  <head>
    <title>Artifacts for gRPC Build <xsl:value-of select="@id"/></title>
    <link rel="stylesheet" type="text/css" href="/web-assets/style.css" />
    <link rel="apple-touch-icon" href="/web-assets/favicons/apple-touch-icon.png" sizes="180x180" />
    <link rel="icon" type="image/png" href="/web-assets/favicons/android-chrome-192x192.png" sizes="192x192" />
    <link rel="icon" type="image/png" href="/web-assets/favicons/favicon-32x32.png" sizes="32x32" />
    <link rel="icon" type="image/png" href="/web-assets/favicons/favicon-16x16.png" sizes="16x16" />
    <link rel="manifest" href="/web-assets/favicons/manifest.json" />
    <link rel="mask-icon" href="/web-assets/favicons/safari-pinned-tab.svg" color="#2DA6B0" />
    <meta name="msapplication-TileColor" content="#ffffff" />
    <meta name="msapplication-TileImage" content="/web-assets/favicons/mstile-150x150.png" />
    <meta name="og:title" content="gRPC Package Build"/>
    <meta name="og:image" content="https://grpc.io/img/grpc_square_reverse_4x.png"/>
    <meta name="og:description" content="gRPC Package Build"/>
 </head>
 <body bgcolor="#ffffff">
 <div id="topbar">
  <span class="title">Artifacts for gRPC Build <xsl:value-of select="@id"/></span>
 </div>
 <div id="main">
  <div id="metadata">
   <span class="fieldname">Build: </span> <a href='#'><xsl:value-of select="@id"/></a>
   [<a href="https://source.cloud.google.com/results/invocations/{@id}">invocation</a>]<br />
  <span class="fieldname">Timestamp: </span>
    <xsl:value-of select="@timestamp"/> <br />
   <span class="fieldname">Branch: </span>
   <a href="https://github.com/grpc/grpc/tree/{./metadata/branch[text()]}">
    <xsl:value-of select="./metadata/branch[text()]" />
   </a><br />
   <span class="fieldname">Commit: </span>
   <a href="https://github.com/grpc/grpc/tree/{./metadata/commit[text()]}">
    <xsl:value-of select="./metadata/commit[text()]" /><br /></a>
  </div>
  <xsl:apply-templates select="artifacts" />
  <br />
  <br />

  <p class="description"><a href="https://grpc.io">gRPC</a> is a <a href="https://www.cncf.io" class="external">Cloud Native Computing Foundation</a> project. <a href="https://policies.google.com/privacy" class="external">Privacy Policy</a>.</p>
  <p class="description">
  Copyright &#169;&#160;<xsl:value-of select="substring(@timestamp, 1, 4)" />&#160;<a href="https://github.com/grpc/grpc/blob/{./metadata/commit[text()]}/AUTHORS">The gRPC Authors</a></p>
  <br />
  <br />
  </div>
 </body>
</html>
</xsl:template>

<xsl:template match="artifacts">
<h2> gRPC <code>protoc</code> Plugins </h2>
<table>
  <xsl:apply-templates select="artifact[@type='protoc']">
    <xsl:sort select="@name" />
  </xsl:apply-templates>
</table>

<h2> C# </h2>
<table>
  <xsl:apply-templates select="artifact[@type='csharp']">
    <xsl:sort select="@name" />
  </xsl:apply-templates>
</table>

<h2> PHP </h2>
<table>
  <xsl:apply-templates select="artifact[@type='php']">
    <xsl:sort select="@name" />
  </xsl:apply-templates>
</table>

<h2> Python </h2>
<script type="text/javascript">
// <![CDATA[
var pythonRepoLink = document.createElement("a");
pythonRepoLink.href = './python';
var pythonRepo = pythonRepoLink.href;
document.write("<p><code>" +
"$ pip install --pre --upgrade --force-reinstall --extra-index-url \\<br />" +
"&nbsp;&nbsp;&nbsp;&nbsp;<a href='" +  pythonRepo + "'>" + pythonRepo + "</a> \\<br />" +
"&nbsp;&nbsp;&nbsp;&nbsp;grpcio grpcio-{tools,health-checking,reflection,testing}</code></p>");
// ]]>
</script>
<table>
  <xsl:apply-templates select="artifact[@type='python']">
    <xsl:sort select="@name" />
  </xsl:apply-templates>
</table>

<h2> Ruby </h2>
<table>
  <xsl:apply-templates select="artifact[@type='ruby']">
    <xsl:sort select="@name" />
  </xsl:apply-templates>
</table>
</xsl:template>


<xsl:template match="artifact">
<tr>
  <td class="name"><a href="{@path}"><xsl:value-of select="@name" /></a></td>
  <td class="hash"><xsl:value-of select="@sha256"/></td>
</tr>
</xsl:template>

<xsl:template match="metadata">
</xsl:template>

</xsl:stylesheet>
