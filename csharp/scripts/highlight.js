//===============================================================================================================
// System  : Color Syntax Highlighter
// File    : Highlight.js
// Author  : Eric Woodruff  (Eric@EWoodruff.us)
// Updated : 10/21/2012
// Note    : Copyright 2006-2012, Eric Woodruff, All rights reserved
//
// This contains the script to expand and collapse the regions in the syntax highlighted code.
//
// This is a customized version for the Sandcastle Help File Builder.  It overrides the CopyCode() function
// from the Hana, Prototype, and VS2005 presentation styles to remove the line numbering and collapsible
// region elements.  The VS2010 style does not currently use the CopyCode() function in here as it has its own
// version for copying the code.
//===============================================================================================================

// Expand/collapse a region
function HighlightExpandCollapse(showId, hideId)
{
    var showSpan = document.getElementById(showId), hideSpan = document.getElementById(hideId);

    showSpan.style.display = "inline";
    hideSpan.style.display = "none";
}

// Copy the code from a colorized code block to the clipboard.
function CopyCode(key)
{
    var idx, line, block, htmlLines, lines, codeText, hasLineNos, hasRegions, clip, trans,
        copyObject, clipID;
    var reLineNo = /^\s*\d{1,4}/;
    var reRegion = /^\s*\d{1,4}\+.*?\d{1,4}-/;
    var reRegionText = /^\+.*?\-/;

    // Find the table row element containing the code
	var trElements = document.getElementsByTagName("tr");

	for(idx = 0; idx < trElements.length; idx++)
		if(key.parentNode.parentNode.parentNode == trElements[idx].parentNode)
		{
		    block = trElements[idx].nextSibling;
		    break;
        }

    if(block.innerText != undefined)
        codeText = block.innerText;
    else
        codeText = block.textContent;

    hasLineNos = block.innerHTML.indexOf("highlight-lineno");
    hasRegions = block.innerHTML.indexOf("highlight-collapsebox");
    htmlLines = block.innerHTML.split("\n");
    lines = codeText.split("\n");

    // Remove the line numbering and collapsible regions if present
    if(hasLineNos != -1 || hasRegions != -1)
    {
        codeText = "";

        for(idx = 0; idx < lines.length; idx++)
        {
            line = lines[idx];

            if(hasRegions && reRegion.test(line))
                line = line.replace(reRegion, "");
            else
            {
                line = line.replace(reLineNo, "");

                // Lines in expanded blocks have an extra space
                if(htmlLines[idx].indexOf("highlight-expanded") != -1 ||
                  htmlLines[idx].indexOf("highlight-endblock") != -1)
                    line = line.substr(1);
            }

            if(hasRegions && reRegionText.test(line))
                line = line.replace(reRegionText, "");

            codeText += line;

            // Not all browsers keep the line feed when split
            if(line[line.length - 1] != "\n")
                codeText += "\n";
        }
    }

    // IE or FireFox/Netscape?
    if(window.clipboardData)
        window.clipboardData.setData("Text", codeText);
    else
        if(window.netscape)
        {
            // Give unrestricted access to browser APIs using XPConnect
            try
            {
                netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
            }
            catch(e)
            {
                alert("Universal Connect was refused, cannot copy to clipboard.  Go to about:config and set " +
                    "signed.applets.codebase_principal_support to true to enable clipboard support.");
                return;
            }

            // Creates an instance of nsIClipboard
            clip = Components.classes["@mozilla.org/widget/clipboard;1"].createInstance(
                Components.interfaces.nsIClipboard);

            // Creates an instance of nsITransferable
            if(clip)
                trans = Components.classes["@mozilla.org/widget/transferable;1"].createInstance(
                    Components.interfaces.nsITransferable);

            if(!trans)
            {
                alert("Copy to Clipboard is not supported by this browser");
                return;
            }

            // Register the data flavor
            trans.addDataFlavor("text/unicode");

            // Create object to hold the data
            copyObject = new Object();

            // Creates an instance of nsISupportsString
            copyObject = Components.classes["@mozilla.org/supports-string;1"].createInstance(
                Components.interfaces.nsISupportsString);

            // Assign the data to be copied
            copyObject.data = codeText;

            // Add data objects to transferable
            trans.setTransferData("text/unicode", copyObject, codeText.length * 2);

            clipID = Components.interfaces.nsIClipboard;

            if(!clipID)
            {
                alert("Copy to Clipboard is not supported by this browser");
                return;
            }

            // Transfer the data to the clipboard
            clip.setData(trans, null, clipID.kGlobalClipboard);
        }
        else
            alert("Copy to Clipboard is not supported by this browser");
}
