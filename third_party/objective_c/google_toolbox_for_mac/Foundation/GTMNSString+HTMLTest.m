//
//  GTMNSString+HTMLTest.m
//
//  Copyright 2005-2008 Google Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may obtain a copy
//  of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
//  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
//  License for the specific language governing permissions and limitations under
//  the License.
//

#import "GTMSenTestCase.h"
#import "GTMNSString+HTML.h"

@interface GTMNSString_HTMLTest : GTMTestCase
@end

@implementation GTMNSString_HTMLTest

- (void)testStringByEscapingHTML {
  unichar chars[] =
  { 34, 38, 39, 60, 62, 338, 339, 352, 353, 376, 710, 732,
    8194, 8195, 8201, 8204, 8205, 8206, 8207, 8211, 8212, 8216, 8217, 8218,
    8220, 8221, 8222, 8224, 8225, 8240, 8249, 8250, 8364,  };

  NSString *string1 = [NSString stringWithCharacters:chars
                                              length:sizeof(chars) / sizeof(unichar)];
  NSString *string2 =
    @"&quot;&amp;&apos;&lt;&gt;&OElig;&oelig;&Scaron;&scaron;&Yuml;"
     "&circ;&tilde;&ensp;&emsp;&thinsp;&zwnj;&zwj;&lrm;&rlm;&ndash;"
     "&mdash;&lsquo;&rsquo;&sbquo;&ldquo;&rdquo;&bdquo;&dagger;&Dagger;"
     "&permil;&lsaquo;&rsaquo;&euro;";

  XCTAssertEqualObjects([string1 gtm_stringByEscapingForHTML],
                        string2,
                        @"HTML escaping failed");

  XCTAssertEqualObjects([@"<this & that>" gtm_stringByEscapingForHTML],
                        @"&lt;this &amp; that&gt;",
                        @"HTML escaping failed");
  NSString *string = [NSString stringWithUTF8String:"パン・&ド・カンパーニュ"];
  NSString *escapeStr = [NSString stringWithUTF8String:"パン・&amp;ド・カンパーニュ"];
  XCTAssertEqualObjects([string gtm_stringByEscapingForHTML],
                        escapeStr,
                        @"HTML escaping failed");

  string = [NSString stringWithUTF8String:"abcا1ب<تdef&"];
  XCTAssertEqualObjects([string gtm_stringByEscapingForHTML],
                        [NSString stringWithUTF8String:"abcا1ب&lt;تdef&amp;"],
                        @"HTML escaping failed");

  // test empty string
  XCTAssertEqualObjects([@"" gtm_stringByEscapingForHTML], @"");
} // testStringByEscapingHTML

- (void)testStringByEscapingAsciiHTML {
  unichar chars[] =
  { 34, 38, 39, 60, 62, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170,
    171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185,
    186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200,
    201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215,
    216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230,
    231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245,
    246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 338, 339, 352, 353, 376,
    402, 710, 732, 913, 914, 915, 916, 917, 918, 919, 920, 921, 922, 923, 924,
    925, 926, 927, 928, 929, 931, 932, 933, 934, 935, 936, 937, 945, 946, 947,
    948, 949, 950, 951, 952, 953, 954, 955, 956, 957, 958, 959, 960, 961, 962,
    963, 964, 965, 966, 967, 968, 969, 977, 978, 982, 8194, 8195, 8201, 8204,
    8205, 8206, 8207, 8211, 8212, 8216, 8217, 8218, 8220, 8221, 8222, 8224, 8225,
    8226, 8230, 8240, 8242, 8243, 8249, 8250, 8254, 8260, 8364, 8472, 8465, 8476,
    8482, 8501, 8592, 8593, 8594, 8595, 8596, 8629, 8656, 8657, 8658, 8659, 8660,
    8704, 8706, 8707, 8709, 8711, 8712, 8713, 8715, 8719, 8721, 8722, 8727, 8730,
    8733, 8734, 8736, 8743, 8744, 8745, 8746, 8747, 8756, 8764, 8773, 8776, 8800,
    8801, 8804, 8805, 8834, 8835, 8836, 8838, 8839, 8853, 8855, 8869, 8901, 8968,
    8969, 8970, 8971, 9001, 9002, 9674, 9824, 9827, 9829, 9830 };

  NSString *string1 = [NSString stringWithCharacters:chars
                                              length:sizeof(chars) / sizeof(unichar)];
  NSString *string2 =
    @"&quot;&amp;&apos;&lt;&gt;&nbsp;&iexcl;&cent;&pound;&curren;&yen;"
    "&brvbar;&sect;&uml;&copy;&ordf;&laquo;&not;&shy;&reg;&macr;&deg;"
    "&plusmn;&sup2;&sup3;&acute;&micro;&para;&middot;&cedil;&sup1;"
    "&ordm;&raquo;&frac14;&frac12;&frac34;&iquest;&Agrave;&Aacute;"
    "&Acirc;&Atilde;&Auml;&Aring;&AElig;&Ccedil;&Egrave;&Eacute;"
    "&Ecirc;&Euml;&Igrave;&Iacute;&Icirc;&Iuml;&ETH;&Ntilde;&Ograve;"
    "&Oacute;&Ocirc;&Otilde;&Ouml;&times;&Oslash;&Ugrave;&Uacute;"
    "&Ucirc;&Uuml;&Yacute;&THORN;&szlig;&agrave;&aacute;&acirc;&atilde;"
    "&auml;&aring;&aelig;&ccedil;&egrave;&eacute;&ecirc;&euml;&igrave;"
    "&iacute;&icirc;&iuml;&eth;&ntilde;&ograve;&oacute;&ocirc;&otilde;"
    "&ouml;&divide;&oslash;&ugrave;&uacute;&ucirc;&uuml;&yacute;&thorn;"
    "&yuml;&OElig;&oelig;&Scaron;&scaron;&Yuml;&fnof;&circ;&tilde;"
    "&Alpha;&Beta;&Gamma;&Delta;&Epsilon;&Zeta;&Eta;&Theta;&Iota;"
    "&Kappa;&Lambda;&Mu;&Nu;&Xi;&Omicron;&Pi;&Rho;&Sigma;&Tau;"
    "&Upsilon;&Phi;&Chi;&Psi;&Omega;&alpha;&beta;&gamma;&delta;"
    "&epsilon;&zeta;&eta;&theta;&iota;&kappa;&lambda;&mu;&nu;&xi;"
    "&omicron;&pi;&rho;&sigmaf;&sigma;&tau;&upsilon;&phi;&chi;&psi;"
    "&omega;&thetasym;&upsih;&piv;&ensp;&emsp;&thinsp;&zwnj;&zwj;"
    "&lrm;&rlm;&ndash;&mdash;&lsquo;&rsquo;&sbquo;&ldquo;&rdquo;"
    "&bdquo;&dagger;&Dagger;&bull;&hellip;&permil;&prime;&Prime;"
    "&lsaquo;&rsaquo;&oline;&frasl;&euro;&weierp;&image;&real;&trade;"
    "&alefsym;&larr;&uarr;&rarr;&darr;&harr;&crarr;&lArr;&uArr;&rArr;"
    "&dArr;&hArr;&forall;&part;&exist;&empty;&nabla;&isin;&notin;&ni;"
    "&prod;&sum;&minus;&lowast;&radic;&prop;&infin;&ang;&and;&or;"
    "&cap;&cup;&int;&there4;&sim;&cong;&asymp;&ne;&equiv;&le;&ge;"
    "&sub;&sup;&nsub;&sube;&supe;&oplus;&otimes;&perp;&sdot;&lceil;"
    "&rceil;&lfloor;&rfloor;&lang;&rang;&loz;&spades;&clubs;&hearts;"
    "&diams;";

  XCTAssertEqualObjects([string1 gtm_stringByEscapingForAsciiHTML],
                        string2,
                        @"HTML escaping failed");

  XCTAssertEqualObjects([@"<this & that>" gtm_stringByEscapingForAsciiHTML],
                        @"&lt;this &amp; that&gt;",
                        @"HTML escaping failed");
  NSString *string = [NSString stringWithUTF8String:"パン・ド・カンパーニュ"];
  XCTAssertEqualObjects([string gtm_stringByEscapingForAsciiHTML],
                        @"&#12497;&#12531;&#12539;&#12489;&#12539;&#12459;"
                        "&#12531;&#12497;&#12540;&#12491;&#12517;",
                        @"HTML escaping failed");

  // Mix in some right - to left
  string = [NSString stringWithUTF8String:"abcا1ب<تdef&"];
  XCTAssertEqualObjects([string gtm_stringByEscapingForAsciiHTML],
                        @"abc&#1575;1&#1576;&lt;&#1578;def&amp;",
                        @"HTML escaping failed");
} // stringByEscapingAsciiHTML

- (void)testStringByUnescapingHTML {
  NSString *string1 =
  @"&quot;&amp;&apos;&lt;&gt;&nbsp;&iexcl;&cent;&pound;&curren;&yen;"
  "&brvbar;&sect;&uml;&copy;&ordf;&laquo;&not;&shy;&reg;&macr;&deg;"
  "&plusmn;&sup2;&sup3;&acute;&micro;&para;&middot;&cedil;&sup1;"
  "&ordm;&raquo;&frac14;&frac12;&frac34;&iquest;&Agrave;&Aacute;"
  "&Acirc;&Atilde;&Auml;&Aring;&AElig;&Ccedil;&Egrave;&Eacute;"
  "&Ecirc;&Euml;&Igrave;&Iacute;&Icirc;&Iuml;&ETH;&Ntilde;&Ograve;"
  "&Oacute;&Ocirc;&Otilde;&Ouml;&times;&Oslash;&Ugrave;&Uacute;"
  "&Ucirc;&Uuml;&Yacute;&THORN;&szlig;&agrave;&aacute;&acirc;&atilde;"
  "&auml;&aring;&aelig;&ccedil;&egrave;&eacute;&ecirc;&euml;&igrave;"
  "&iacute;&icirc;&iuml;&eth;&ntilde;&ograve;&oacute;&ocirc;&otilde;"
  "&ouml;&divide;&oslash;&ugrave;&uacute;&ucirc;&uuml;&yacute;&thorn;"
  "&yuml;&OElig;&oelig;&Scaron;&scaron;&Yuml;&fnof;&circ;&tilde;"
  "&Alpha;&Beta;&Gamma;&Delta;&Epsilon;&Zeta;&Eta;&Theta;&Iota;"
  "&Kappa;&Lambda;&Mu;&Nu;&Xi;&Omicron;&Pi;&Rho;&Sigma;&Tau;"
  "&Upsilon;&Phi;&Chi;&Psi;&Omega;&alpha;&beta;&gamma;&delta;"
  "&epsilon;&zeta;&eta;&theta;&iota;&kappa;&lambda;&mu;&nu;&xi;"
  "&omicron;&pi;&rho;&sigmaf;&sigma;&tau;&upsilon;&phi;&chi;&psi;"
  "&omega;&thetasym;&upsih;&piv;&ensp;&emsp;&thinsp;&zwnj;&zwj;"
  "&lrm;&rlm;&ndash;&mdash;&lsquo;&rsquo;&sbquo;&ldquo;&rdquo;"
  "&bdquo;&dagger;&Dagger;&bull;&hellip;&permil;&prime;&Prime;"
  "&lsaquo;&rsaquo;&oline;&frasl;&euro;&weierp;&image;&real;&trade;"
  "&alefsym;&larr;&uarr;&rarr;&darr;&harr;&crarr;&lArr;&uArr;&rArr;"
  "&dArr;&hArr;&forall;&part;&exist;&empty;&nabla;&isin;&notin;&ni;"
  "&prod;&sum;&minus;&lowast;&radic;&prop;&infin;&ang;&and;&or;"
  "&cap;&cup;&int;&there4;&sim;&cong;&asymp;&ne;&equiv;&le;&ge;"
  "&sub;&sup;&nsub;&sube;&supe;&oplus;&otimes;&perp;&sdot;&lceil;"
  "&rceil;&lfloor;&rfloor;&lang;&rang;&loz;&spades;&clubs;&hearts;"
  "&diams;";

  unichar chars[] =
  { 34, 38, 39, 60, 62, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170,
    171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185,
    186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200,
    201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215,
    216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230,
    231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245,
    246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 338, 339, 352, 353, 376,
    402, 710, 732, 913, 914, 915, 916, 917, 918, 919, 920, 921, 922, 923, 924,
    925, 926, 927, 928, 929, 931, 932, 933, 934, 935, 936, 937, 945, 946, 947,
    948, 949, 950, 951, 952, 953, 954, 955, 956, 957, 958, 959, 960, 961, 962,
    963, 964, 965, 966, 967, 968, 969, 977, 978, 982, 8194, 8195, 8201, 8204,
    8205, 8206, 8207, 8211, 8212, 8216, 8217, 8218, 8220, 8221, 8222, 8224, 8225,
    8226, 8230, 8240, 8242, 8243, 8249, 8250, 8254, 8260, 8364, 8472, 8465, 8476,
    8482, 8501, 8592, 8593, 8594, 8595, 8596, 8629, 8656, 8657, 8658, 8659, 8660,
    8704, 8706, 8707, 8709, 8711, 8712, 8713, 8715, 8719, 8721, 8722, 8727, 8730,
    8733, 8734, 8736, 8743, 8744, 8745, 8746, 8747, 8756, 8764, 8773, 8776, 8800,
    8801, 8804, 8805, 8834, 8835, 8836, 8838, 8839, 8853, 8855, 8869, 8901, 8968,
    8969, 8970, 8971, 9001, 9002, 9674, 9824, 9827, 9829, 9830 };

  NSString *string2 = [NSString stringWithCharacters:chars
                                              length:sizeof(chars) / sizeof(unichar)];
  XCTAssertEqualObjects([string1 gtm_stringByUnescapingFromHTML],
                        string2,
                        @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&#65;&#x42;&#X43;" gtm_stringByUnescapingFromHTML],
                        @"ABC", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"" gtm_stringByUnescapingFromHTML],
                        @"", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&#65;&Bang;&#X43;" gtm_stringByUnescapingFromHTML],
                        @"A&Bang;C", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&#65&Bang;&#X43;" gtm_stringByUnescapingFromHTML],
                        @"&#65&Bang;C", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&#65;&Bang;&#X43" gtm_stringByUnescapingFromHTML],
                        @"A&Bang;&#X43", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&#65A;" gtm_stringByUnescapingFromHTML],
                        @"&#65A;", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&" gtm_stringByUnescapingFromHTML],
                        @"&", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&;" gtm_stringByUnescapingFromHTML],
                        @"&;", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&x;" gtm_stringByUnescapingFromHTML],
                        @"&x;", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&X;" gtm_stringByUnescapingFromHTML],
                        @"&X;", @"HTML unescaping failed");

  XCTAssertEqualObjects([@";" gtm_stringByUnescapingFromHTML],
                        @";", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&lt;this &amp; that&gt;" gtm_stringByUnescapingFromHTML],
                        @"<this & that>", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&#x10437;" gtm_stringByUnescapingFromHTML],
                        @"𐐷", @"HTML unescaping failed");

  XCTAssertEqualObjects([@"&#66615;" gtm_stringByUnescapingFromHTML],
                        @"𐐷", @"HTML unescaping failed");

} // testStringByUnescapingHTML

- (void)testStringRoundtrippingEscapedHTML {
  NSString *string = [NSString stringWithUTF8String:"This test ©™®๒०᠐٧"];
  XCTAssertEqualObjects(string,
                        [[string gtm_stringByEscapingForHTML] gtm_stringByUnescapingFromHTML],
                        @"HTML Roundtripping failed");
  string = [NSString stringWithUTF8String:"This test ©™®๒०᠐٧"];
  XCTAssertEqualObjects(string,
                        [[string gtm_stringByEscapingForAsciiHTML] gtm_stringByUnescapingFromHTML],
                        @"HTML Roundtripping failed");
}

@end
