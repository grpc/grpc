//===============================================================================================================
// System  : Sandcastle Help File Builder
// File    : branding.js
// Author  : Eric Woodruff  (Eric@EWoodruff.us)
// Updated : 10/08/2015
// Note    : Copyright 2014-2015, Eric Woodruff, All rights reserved
//           Portions Copyright 2010-2014 Microsoft, All rights reserved
//
// This file contains the methods necessary to implement the language filtering, collapsible section, and
// copy to clipboard options.
//
// This code is published under the Microsoft Public License (Ms-PL).  A copy of the license should be
// distributed with the code and can be found at the project website: https://GitHub.com/EWSoftware/SHFB.  This
// notice, the author's name, and all copyright notices must remain intact in all applications, documentation,
// and source files.
//
//    Date     Who  Comments
// ==============================================================================================================
// 05/04/2014  EFW  Created the code based on the MS Help Viewer script
//===============================================================================================================

// The IDs of all code snippet sets on the same page are stored so that we can keep them in synch when a tab is
// selected.
var allTabSetIds = new Array();

// The IDs of language-specific text (LST) spans are used as dictionary keys so that we can get access to the
// spans and update them when the user changes to a different language tab.  The values of the dictionary
// objects are pipe separated language-specific attributes (lang1=value|lang2=value|lang3=value).  The language
// ID can be specific (cs, vb, cpp, etc.) or may be a neutral entry (nu) which specifies text common to multiple
// languages.  If a language is not present and there is no neutral entry, the span is hidden for all languages
// to which it does not apply.
var allLSTSetIds = new Object();

// Help 1 persistence support.  This code must appear inline.
var isHelp1;

var curLoc = document.location + ".";

if(curLoc.indexOf("mk:@MSITStore") == 0)
{
    isHelp1 = true;
    curLoc = "ms-its:" + curLoc.substring(14, curLoc.length - 1);
    document.location.replace(curLoc);
}
else
    if(curLoc.indexOf("ms-its:") == 0)
        isHelp1 = true;
    else
        isHelp1 = false;

// The OnLoad method
function OnLoad(defaultLanguage)
{
    var defLang;

    if(typeof (defaultLanguage) == "undefined" || defaultLanguage == null || defaultLanguage == "")
        defLang = "vb";
    else
        defLang = defaultLanguage;

    // In MS Help Viewer, the transform the topic is ran through can move the footer.  Move it back where it
    // belongs if necessary.
    try
    {
        var footer = document.getElementById("pageFooter")

        if(footer)
        {
            var footerParent = document.body;

            if(footer.parentElement != footerParent)
            {
                footer.parentElement.removeChild(footer);
                footerParent.appendChild(footer);
            }
        }
    }
    catch(e)
    {
    }

    var language = GetCookie("CodeSnippetContainerLanguage", defLang);

    // If LST exists on the page, set the LST to show the user selected programming language
    UpdateLST(language);

    // If code snippet groups exist, set the current language for them
    if(allTabSetIds.length > 0)
    {
        var i = 0;

        while(i < allTabSetIds.length)
        {
            var tabCount = 1;

            // The tab count may vary so find the last one in this set
            while(document.getElementById(allTabSetIds[i] + "_tab" + tabCount) != null)
                tabCount++;

            tabCount--;

            // If not grouped, skip it
            if(tabCount < 2)
            {
                // Disable the Copy Code link if in Chrome
                if(navigator.userAgent.toLowerCase().indexOf("chrome") != -1)
                    document.getElementById(allTabSetIds[i] + "_copyCode").style.display = "none";
            }
            else
                SetCurrentLanguage(allTabSetIds[i], language, tabCount);

            i++;
        }
    }

    InitializeToc();
}

// This is just a place holder.  The website script implements this function to initialize it's in-page TOC pane
function InitializeToc()
{
}

// This function executes in the OnLoad event and ChangeTab action on code snippets.  The function parameter
// is the user chosen programming language.  This function iterates through the "allLSTSetIds" dictionary object
// to update the node value of the LST span tag per the user's chosen programming language.
function UpdateLST(language)
{
    for(var lstMember in allLSTSetIds)
    {
        var devLangSpan = document.getElementById(lstMember);

        if(devLangSpan != null)
        {
            // There may be a carriage return before the LST span in the content so the replace function below
            // is used to trim the whitespace at the end of the previous node of the current LST node.
            if(devLangSpan.previousSibling != null && devLangSpan.previousSibling.nodeValue != null)
                devLangSpan.previousSibling.nodeValue = devLangSpan.previousSibling.nodeValue.replace(/\s+$/, "");

            var langs = allLSTSetIds[lstMember].split("|");
            var k = 0;
            var keyValue;

            while(k < langs.length)
            {
                keyValue = langs[k].split("=");

                if(keyValue[0] == language)
                {
                    devLangSpan.innerHTML = keyValue[1];

                    // Help 1 and MS Help Viewer workaround.  Add a space if the following text element starts
                    // with a space to prevent things running together.
                    if(devLangSpan.parentNode != null && devLangSpan.parentNode.nextSibling != null)
                    {
                        if (devLangSpan.parentNode.nextSibling.nodeValue != null &&
                          !devLangSpan.parentNode.nextSibling.nodeValue.substring(0, 1).match(/[.,);:!/?]/))
                        {
                            devLangSpan.innerHTML = keyValue[1] + " ";
                        }
                    }
                    break;
                }

                k++;
            }

            // If not found, default to the neutral language.  If there is no neutral language entry, clear the
            // content to hide it.
            if(k >= langs.length)
            {
                if(language != "nu")
                {
                    k = 0;

                    while(k < langs.length)
                    {
                        keyValue = langs[k].split("=");

                        if(keyValue[0] == "nu")
                        {
                            devLangSpan.innerHTML = keyValue[1];

                            // Help 1 and MS Help Viewer workaround.  Add a space if the following text element
                            // starts with a space to prevent things running together.
                            if(devLangSpan.parentNode != null && devLangSpan.parentNode.nextSibling != null)
                            {
                                if(devLangSpan.parentNode.nextSibling.nodeValue != null &&
                                  !devLangSpan.parentNode.nextSibling.nodeValue.substring(0, 1).match(/[.,);:!/?]/))
                                {
                                    devLangSpan.innerHTML = keyValue[1] + " ";
                                }
                            }
                            break;
                        }

                        k++;
                    }
                }

                if(k >= langs.length)
                    devLangSpan.innerHTML = "";
            }
        }
    }
}

// Get the specified cookie.  If not found, return the specified default value.
function GetCookie(cookieName, defaultValue)
{
    if(isHelp1)
    {
        try
        {
            var globals = Help1Globals;

            var value = globals.Load(cookieName);

            if(value == null)
                value = defaultValue;

            return value;
        }
        catch(e)
        {
            return defaultValue;
        }
    }

    var cookie = document.cookie.split("; ");

    for(var i = 0; i < cookie.length; i++)
    {
        var crumb = cookie[i].split("=");

        if(cookieName == crumb[0])
            return unescape(crumb[1])
    }

    return defaultValue;
}

// Set the specified cookie to the specified value
function SetCookie(name, value)
{
    if(isHelp1)
    {
        try
        {
            var globals = Help1Globals;

            globals.Save(name, value);
        }
        catch(e)
        {
        }

        return;
    }

    var today = new Date();

    today.setTime(today.getTime());

    // Set the expiration time to be 60 days from now (in milliseconds)
    var expires_date = new Date(today.getTime() + (60 * 1000 * 60 * 60 * 24));

    document.cookie = name + "=" + escape(value) + ";expires=" + expires_date.toGMTString() + ";path=/";
}

// Add a language-specific text ID
function AddLanguageSpecificTextSet(lstId)
{
    var keyValue = lstId.split("?")

    allLSTSetIds[keyValue[0]] = keyValue[1];
}

// Add a language tab set ID
function AddLanguageTabSet(tabSetId)
{
    allTabSetIds.push(tabSetId);
}

// Switch the active tab for all of other code snippets
function ChangeTab(tabSetId, language, snippetIdx, snippetCount)
{
    SetCookie("CodeSnippetContainerLanguage", language);

    SetActiveTab(tabSetId, snippetIdx, snippetCount);

    // If LST exists on the page, set the LST to show the user selected programming language
    UpdateLST(language);

    var i = 0;

    while(i < allTabSetIds.length)
    {
        // We just care about other snippets
        if(allTabSetIds[i] != tabSetId)
        {
            // Other tab sets may not have the same number of tabs
            var tabCount = 1;

            while(document.getElementById(allTabSetIds[i] + "_tab" + tabCount) != null)
                tabCount++;

            tabCount--;

            // If not grouped, skip it
            if(tabCount > 1)
                SetCurrentLanguage(allTabSetIds[i], language, tabCount);
        }

        i++;
    }
}

// Sets the current language in the specified tab set
function SetCurrentLanguage(tabSetId, language, tabCount)
{
    var tabIndex = 1;

    while(tabIndex <= tabCount)
    {
        var tabTemp = document.getElementById(tabSetId + "_tab" + tabIndex);

        if(tabTemp != null && tabTemp.innerHTML.indexOf("'" + language + "'") != -1)
            break;

        tabIndex++;
    }

    if(tabIndex > tabCount)
    {
        // Select the first non-disabled tab
        tabIndex = 1;

        if(document.getElementById(tabSetId + "_tab1").className == "codeSnippetContainerTabPhantom")
        {
            tabIndex++;

            while(tabIndex <= tabCount)
            {
                var tab = document.getElementById(tabSetId + "_tab" + tabIndex);

                if(tab.className != "codeSnippetContainerTabPhantom")
                {
                    tab.className = "codeSnippetContainerTabActive";
                    document.getElementById(tabSetId + "_code_Div" + j).style.display = "block";
                    break;
                }

                tabIndex++;
            }
        }
    }

    SetActiveTab(tabSetId, tabIndex, tabCount);
}

// Set the active tab within a tab set
function SetActiveTab(tabSetId, tabIndex, tabCount)
{
    var i = 1;

    while(i <= tabCount)
    {
        var tabTemp = document.getElementById(tabSetId + "_tab" + i);

        if (tabTemp != null)
        {
            if(tabTemp.className == "codeSnippetContainerTabActive")
                tabTemp.className = "codeSnippetContainerTab";
            else
                if(tabTemp.className == "codeSnippetContainerTabPhantom")
                    tabTemp.style.display = "none";

            var codeTemp = document.getElementById(tabSetId + "_code_Div" + i);

            if(codeTemp.style.display != "none")
                codeTemp.style.display = "none";
        }

        i++;
    }

    // Phantom tabs are shown or hidden as needed
    if(document.getElementById(tabSetId + "_tab" + tabIndex).className != "codeSnippetContainerTabPhantom")
        document.getElementById(tabSetId + "_tab" + tabIndex).className = "codeSnippetContainerTabActive";
    else
        document.getElementById(tabSetId + "_tab" + tabIndex).style.display = "block";

    document.getElementById(tabSetId + "_code_Div" + tabIndex).style.display = "block";

    // Show copy code button if not in Chrome
    if(navigator.userAgent.toLowerCase().indexOf("chrome") == -1)
        document.getElementById(tabSetId + "_copyCode").style.display = "inline";
    else
        document.getElementById(tabSetId + "_copyCode").style.display = "none";
}

// Copy the code from the active tab of the given tab set to the clipboard
function CopyToClipboard(tabSetId)
{
    var tabTemp, contentId;
    var i = 1;

    do
    {
        contentId = tabSetId + "_code_Div" + i;
        tabTemp = document.getElementById(contentId);

        if(tabTemp != null && tabTemp.style.display != "none")
            break;

        i++;

    } while(tabTemp != null);

    if(tabTemp == null)
        return;

    if(window.clipboardData)
    {
        try
        {
            window.clipboardData.setData("Text", document.getElementById(contentId).innerText);
        }
        catch(e)
        {
            alert("Permission denied. Enable copying to the clipboard.");
        }
    }
    else if(window.netscape)
    {
        try
        {
            netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");

            var clip = Components.classes["@mozilla.org/widget/clipboard;1"].createInstance(
                Components.interfaces.nsIClipboard);

            if(!clip)
                return;

            var trans = Components.classes["@mozilla.org/widget/transferable;1"].createInstance(
                Components.interfaces.nsITransferable);

            if(!trans)
                return;

            trans.addDataFlavor("text/unicode");

            var str = new Object();
            var len = new Object();
            var str = Components.classes["@mozilla.org/supports-string;1"].createInstance(
                Components.interfaces.nsISupportsString);

            var copytext = document.getElementById(contentId).textContent;

            str.data = copytext;
            trans.setTransferData("text/unicode", str, copytext.length * 2);

            var clipid = Components.interfaces.nsIClipboard;

            clip.setData(trans, null, clipid.kGlobalClipboard);
        }
        catch(e)
        {
            alert("Permission denied. Enter \"about:config\" in the address bar and double-click the \"signed.applets.codebase_principal_support\" setting to enable copying to the clipboard.");
        }
    }
}

// Expand or collapse a section
function SectionExpandCollapse(togglePrefix)
{
    var image = document.getElementById(togglePrefix + "Toggle");
    var section = document.getElementById(togglePrefix + "Section");

    if(image != null && section != null)
        if(section.style.display == "")
        {
            image.src = image.src.replace("SectionExpanded.png", "SectionCollapsed.png");
            section.style.display = "none";
        }
        else
        {
            image.src = image.src.replace("SectionCollapsed.png", "SectionExpanded.png");
            section.style.display = "";
        }
}

// Expand or collapse a section when it has the focus and Enter is hit
function SectionExpandCollapse_CheckKey(togglePrefix, eventArgs)
{
    if(eventArgs.keyCode == 13)
        SectionExpandCollapse(togglePrefix);
}

// Help 1 persistence object.  This requires a hidden input element on the page with a class of "userDataStyle"
// defined in the style sheet that implements the user data binary behavior:
// <input type="hidden" id="userDataCache" class="userDataStyle" />
var Help1Globals =
{
    UserDataCache: function()
    {
        var userData = document.getElementById("userDataCache");

        return userData;
    },

    Load: function(key)
    {
        var userData = this.UserDataCache();

        userData.load("userDataSettings");

        var value = userData.getAttribute(key);

        return value;
    },

    Save: function(key, value)
    {
        var userData = this.UserDataCache();
        userData.setAttribute(key, value);
        userData.save("userDataSettings");
    }
};
