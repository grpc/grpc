//===============================================================================================================
// System  : Sandcastle Help File Builder
// File    : branding-Website.js
// Author  : Eric Woodruff  (Eric@EWoodruff.us)
// Updated : 03/04/2015
// Note    : Copyright 2014-2015, Eric Woodruff, All rights reserved
//           Portions Copyright 2014 Sam Harwell, All rights reserved
//
// This file contains the methods necessary to implement the lightweight TOC and search functionality.
//
// This code is published under the Microsoft Public License (Ms-PL).  A copy of the license should be
// distributed with the code.  It can also be found at the project website: https://GitHub.com/EWSoftware/SHFB.  This
// notice, the author's name, and all copyright notices must remain intact in all applications, documentation,
// and source files.
//
//    Date     Who  Comments
// ==============================================================================================================
// 05/04/2014  EFW  Created the code based on a combination of the lightweight TOC code from Sam Harwell and
//                  the existing search code from SHFB.
//===============================================================================================================

// Width of the TOC
var tocWidth;

// Search method (0 = To be determined, 1 = ASPX, 2 = PHP, anything else = client-side script
var searchMethod = 0;

// Table of contents script

// Initialize the TOC by restoring its width from the cookie if present
function InitializeToc()
{
    tocWidth = parseInt(GetCookie("TocWidth", "280"));
    ResizeToc();
    $(window).resize(SetNavHeight)
}

function SetNavHeight()
{
    $leftNav = $("#leftNav")
    $topicContent = $("#TopicContent")
    leftNavPadding = $leftNav.outerHeight() - $leftNav.height()
    contentPadding = $topicContent.outerHeight() - $topicContent.height()
    // want outer height of left navigation div to match outer height of content
    leftNavHeight = $topicContent.outerHeight() - leftNavPadding
    $leftNav.css("min-height", leftNavHeight + "px")
}

// Increase the TOC width
function OnIncreaseToc()
{
    if(tocWidth < 1)
        tocWidth = 280;
    else
        tocWidth += 100;

    if(tocWidth > 680)
        tocWidth = 0;

    ResizeToc();
    SetCookie("TocWidth", tocWidth);
}

// Reset the TOC to its default width
function OnResetToc()
{
    tocWidth = 0;

    ResizeToc();
    SetCookie("TocWidth", tocWidth);
}

// Resize the TOC width
function ResizeToc()
{
    var toc = document.getElementById("leftNav");

    if(toc)
    {
        // Set TOC width
        toc.style.width = tocWidth + "px";

        var leftNavPadding = 10;

        document.getElementById("TopicContent").style.marginLeft = (tocWidth + leftNavPadding) + "px";

        // Position images
        document.getElementById("TocResize").style.left = (tocWidth + leftNavPadding) + "px";

        // Hide/show increase TOC width image
        document.getElementById("ResizeImageIncrease").style.display = (tocWidth >= 680) ? "none" : "";

        // Hide/show reset TOC width image
        document.getElementById("ResizeImageReset").style.display = (tocWidth < 680) ? "none" : "";
    }

    SetNavHeight()
}

// Toggle a TOC entry between its collapsed and expanded state
function Toggle(item)
{
    var isExpanded = $(item).hasClass("tocExpanded");

    $(item).toggleClass("tocExpanded tocCollapsed");

    if(isExpanded)
    {
        Collapse($(item).parent());
    }
    else
    {
        var childrenLoaded = $(item).parent().attr("data-childrenloaded");

        if(childrenLoaded)
        {
            Expand($(item).parent());
        }
        else
        {
            var tocid = $(item).next().attr("tocid");

            $.ajax({
                url: "../toc/" + tocid + ".xml",
                async: true,
                dataType: "xml",
                success: function(data)
                {
                    BuildChildren($(item).parent(), data);
                }
            });
        }
    }
}

// HTML encode a value for use on the page
function HtmlEncode(value)
{
    // Create an in-memory div, set it's inner text (which jQuery automatically encodes) then grab the encoded
    // contents back out.  The div never exists on the page.
    return $('<div/>').text(value).html();
}

// Build the child entries of a TOC entry
function BuildChildren(tocDiv, data)
{
    var childLevel = +tocDiv.attr("data-toclevel") + 1;
    var childTocLevel = childLevel >= 10 ? 10 : childLevel;
    var elements = data.getElementsByTagName("HelpTOCNode");

    var isRoot = true;

    if(data.getElementsByTagName("HelpTOC").length == 0)
    {
        // The first node is the root node of this group, don't show it again
        isRoot = false;
    }

    for(var i = elements.length - 1; i > 0 || (isRoot && i == 0); i--)
    {
        var childHRef, childId = elements[i].getAttribute("Url");

        if(childId != null && childId.length > 5)
        {
            // The Url attribute has the form "html/{childId}.htm"
            childHRef = childId.substring(5, childId.length);
            childId = childId.substring(5, childId.lastIndexOf("."));
        }
        else
        {
            // The Id attribute is in raw form.  There is no URL (empty container node).  In this case, we'll
            // just ignore it and go nowhere.  It's a rare case that isn't worth trying to get the first child.
            // Instead, we'll just expand the node (see below).
            childHRef = "#";
            childId = elements[i].getAttribute("Id");
        }

        var existingItem = null;

        tocDiv.nextAll().each(function()
        {
            if(!existingItem && $(this).children().last("a").attr("tocid") == childId)
            {
                existingItem = $(this);
            }
        });

        if(existingItem != null)
        {
            // First move the children of the existing item
            var existingChildLevel = +existingItem.attr("data-toclevel");
            var doneMoving = false;
            var inserter = tocDiv;

            existingItem.nextAll().each(function()
            {
                if(!doneMoving && +$(this).attr("data-toclevel") > existingChildLevel)
                {
                    inserter.after($(this));
                    inserter = $(this);
                    $(this).attr("data-toclevel", +$(this).attr("data-toclevel") + childLevel - existingChildLevel);

                    if($(this).hasClass("current"))
                        $(this).attr("class", "toclevel" + (+$(this).attr("data-toclevel") + " current"));
                    else
                        $(this).attr("class", "toclevel" + (+$(this).attr("data-toclevel")));
                }
                else
                {
                    doneMoving = true;
                }
            });

            // Now move the existing item itself
            tocDiv.after(existingItem);
            existingItem.attr("data-toclevel", childLevel);
            existingItem.attr("class", "toclevel" + childLevel);
        }
        else
        {
            var hasChildren = elements[i].getAttribute("HasChildren");
            var childTitle = HtmlEncode(elements[i].getAttribute("Title"));
            var expander = "";

            if(hasChildren)
                expander = "<a class=\"tocCollapsed\" onclick=\"javascript: Toggle(this);\" href=\"#!\"></a>";

            var text = "<div class=\"toclevel" + childTocLevel + "\" data-toclevel=\"" + childLevel + "\">" +
                expander + "<a data-tochassubtree=\"" + hasChildren + "\" href=\"" + childHRef + "\" title=\"" +
                childTitle + "\" tocid=\"" + childId + "\"" +
                (childHRef == "#" ? " onclick=\"javascript: Toggle(this.previousSibling);\"" : "") + ">" +
                childTitle + "</a></div>";

            tocDiv.after(text);
        }
    }

    tocDiv.attr("data-childrenloaded", true);
}

// Collapse a TOC entry
function Collapse(tocDiv)
{
    // Hide all the TOC elements after item, until we reach one with a data-toclevel less than or equal to the
    // current item's value.
    var tocLevel = +tocDiv.attr("data-toclevel");
    var done = false;

    tocDiv.nextAll().each(function()
    {
        if(!done && +$(this).attr("data-toclevel") > tocLevel)
        {
            $(this).hide();
        }
        else
        {
            done = true;
        }
    });
}

// Expand a TOC entry
function Expand(tocDiv)
{
    // Show all the TOC elements after item, until we reach one with a data-toclevel less than or equal to the
    // current item's value
    var tocLevel = +tocDiv.attr("data-toclevel");
    var done = false;

    tocDiv.nextAll().each(function()
    {
        if(done)
        {
            return;
        }

        var childTocLevel = +$(this).attr("data-toclevel");

        if(childTocLevel == tocLevel + 1)
        {
            $(this).show();

            if($(this).children("a").first().hasClass("tocExpanded"))
            {
                Expand($(this));
            }
        }
        else if(childTocLevel > tocLevel + 1)
        {
            // Ignore this node, handled by recursive calls
        }
        else
        {
            done = true;
        }
    });
}

// This is called to prepare for dragging the sizer div
function OnMouseDown(event)
{
    document.addEventListener("mousemove", OnMouseMove, true);
    document.addEventListener("mouseup", OnMouseUp, true);
    event.preventDefault();
}

// Resize the TOC as the sizer is dragged
function OnMouseMove(event)
{
    tocWidth = (event.clientX > 700) ? 700 : (event.clientX < 100) ? 100 : event.clientX;

    ResizeToc();
}

// Finish the drag operation when the mouse button is released
function OnMouseUp(event)
{
    document.removeEventListener("mousemove", OnMouseMove, true);
    document.removeEventListener("mouseup", OnMouseUp, true);

    SetCookie("TocWidth", tocWidth);
}

// Search functions

// Transfer to the search page from a topic
function TransferToSearchPage()
{
    var searchText = document.getElementById("SearchTextBox").value.trim();

    if(searchText.length != 0)
        document.location.replace(encodeURI("../search.html?SearchText=" + searchText));
}

// Initiate a search when the search page loads
function OnSearchPageLoad()
{
    var queryString = decodeURI(document.location.search);

    if(queryString != "")
    {
        var idx, options = queryString.split(/[\?\=\&]/);

        for(idx = 0; idx < options.length; idx++)
            if(options[idx] == "SearchText" && idx + 1 < options.length)
            {
                document.getElementById("txtSearchText").value = options[idx + 1];
                PerformSearch();
                break;
            }
    }
}

// Perform a search using the best available method
function PerformSearch()
{
    var searchText = document.getElementById("txtSearchText").value;
    var sortByTitle = document.getElementById("chkSortByTitle").checked;
    var searchResults = document.getElementById("searchResults");

    if(searchText.length == 0)
    {
        searchResults.innerHTML = "<strong>Nothing found</strong>";
        return;
    }

    searchResults.innerHTML = "Searching...";

    // Determine the search method if not done already.  The ASPX and PHP searches are more efficient as they
    // run asynchronously server-side.  If they can't be used, it defaults to the client-side script below which
    // will work but has to download the index files.  For large help sites, this can be inefficient.
    if(searchMethod == 0)
        searchMethod = DetermineSearchMethod();

    if(searchMethod == 1)
    {
        $.ajax({
            type: "GET",
            url: encodeURI("SearchHelp.aspx?Keywords=" + searchText + "&SortByTitle=" + sortByTitle),
            success: function(html)
            {
                searchResults.innerHTML = html;
            }
        });

        return;
    }

    if(searchMethod == 2)
    {
        $.ajax({
            type: "GET",
            url: encodeURI("SearchHelp.php?Keywords=" + searchText + "&SortByTitle=" + sortByTitle),
            success: function(html)
            {
                searchResults.innerHTML = html;
            }
        });

        return;
    }

    // Parse the keywords
    var keywords = ParseKeywords(searchText);

    // Get the list of files.  We'll be getting multiple files so we need to do this synchronously.
    var fileList = [];

    $.ajax({
        type: "GET",
        url: "fti/FTI_Files.json",
        dataType: "json",
        async: false,
        success: function(data)
        {
            $.each(data, function(key, val)
            {
                fileList[key] = val;
            });
        }
    });

    var letters = [];
    var wordDictionary = {};
    var wordNotFound = false;

    // Load the keyword files for each keyword starting letter
    for(var idx = 0; idx < keywords.length && !wordNotFound; idx++)
    {
        var letter = keywords[idx].substring(0, 1);

        if($.inArray(letter, letters) == -1)
        {
            letters.push(letter);

            $.ajax({
                type: "GET",
                url: "fti/FTI_" + letter.charCodeAt(0) + ".json",
                dataType: "json",
                async: false,
                success: function(data)
                {
                    var wordCount = 0;

                    $.each(data, function(key, val)
                    {
                        wordDictionary[key] = val;
                        wordCount++;
                    });

                    if(wordCount == 0)
                        wordNotFound = true;
                }
            });
        }
    }

    if(wordNotFound)
        searchResults.innerHTML = "<strong>Nothing found</strong>";
    else
        searchResults.innerHTML = SearchForKeywords(keywords, fileList, wordDictionary, sortByTitle);
}

// Determine the search method by seeing if the ASPX or PHP search pages are present and working
function DetermineSearchMethod()
{
    var method = 3;

    try
    {
        $.ajax({
            type: "GET",
            url: "SearchHelp.aspx",
            async: false,
            success: function(html)
            {
                if(html.substring(0, 8) == "<strong>")
                    method = 1;
            }
        });

        if(method == 3)
            $.ajax({
                type: "GET",
                url: "SearchHelp.php",
                async: false,
                success: function(html)
                {
                    if(html.substring(0, 8) == "<strong>")
                        method = 2;
                }
            });
    }
    catch(e)
    {
    }

    return method;
}

// Split the search text up into keywords
function ParseKeywords(keywords)
{
    var keywordList = [];
    var checkWord;
    var words = keywords.split(/\W+/);

    for(var idx = 0; idx < words.length; idx++)
    {
        checkWord = words[idx].toLowerCase();

        if(checkWord.length > 2)
        {
            var charCode = checkWord.charCodeAt(0);

            if((charCode < 48 || charCode > 57) && $.inArray(checkWord, keywordList) == -1)
                keywordList.push(checkWord);
        }
    }

    return keywordList;
}

// Search for keywords and generate a block of HTML containing the results
function SearchForKeywords(keywords, fileInfo, wordDictionary, sortByTitle)
{
    var matches = [], matchingFileIndices = [], rankings = [];
    var isFirst = true;

    for(var idx = 0; idx < keywords.length; idx++)
    {
        var word = keywords[idx];
        var occurrences = wordDictionary[word];

        // All keywords must be found
        if(occurrences == null)
            return "<strong>Nothing found</strong>";

        matches[word] = occurrences;
        var occurrenceIndices = [];

        // Get a list of the file indices for this match.  These are 64-bit numbers but JavaScript only does
        // bit shifts on 32-bit values so we divide by 2^16 to get the same effect as ">> 16" and use floor()
        // to truncate the result.
        for(var ind in occurrences)
            occurrenceIndices.push(Math.floor(occurrences[ind] / Math.pow(2, 16)));

        if(isFirst)
        {
            isFirst = false;

            for(var matchInd in occurrenceIndices)
                matchingFileIndices.push(occurrenceIndices[matchInd]);
        }
        else
        {
            // After the first match, remove files that do not appear for all found keywords
            for(var checkIdx = 0; checkIdx < matchingFileIndices.length; checkIdx++)
                if($.inArray(matchingFileIndices[checkIdx], occurrenceIndices) == -1)
                {
                    matchingFileIndices.splice(checkIdx, 1);
                    checkIdx--;
                }
        }
    }

    if(matchingFileIndices.length == 0)
        return "<strong>Nothing found</strong>";

    // Rank the files based on the number of times the words occurs
    for(var fileIdx = 0; fileIdx < matchingFileIndices.length; fileIdx++)
    {
        // Split out the title, filename, and word count
        var matchingIdx = matchingFileIndices[fileIdx];
        var fileIndex = fileInfo[matchingIdx].split(/\0/);

        var title = fileIndex[0];
        var filename = fileIndex[1];
        var wordCount = parseInt(fileIndex[2]);
        var matchCount = 0;

        for(var idx = 0; idx < keywords.length; idx++)
        {
            occurrences = matches[keywords[idx]];

            for(var ind in occurrences)
            {
                var entry = occurrences[ind];

                // These are 64-bit numbers but JavaScript only does bit shifts on 32-bit values so we divide
                // by 2^16 to get the same effect as ">> 16" and use floor() to truncate the result.
                if(Math.floor(entry / Math.pow(2, 16)) == matchingIdx)
                    matchCount += (entry & 0xFFFF);
            }
        }

        rankings.push({ Filename: filename, PageTitle: title, Rank: matchCount * 1000 / wordCount });

        if(rankings.length > 99)
            break;
    }

    rankings.sort(function(x, y)
    {
        if(!sortByTitle)
            return y.Rank - x.Rank;

        return x.PageTitle.localeCompare(y.PageTitle);
    });

    // Format and return the results
    var content = "<ol>";

    for(var r in rankings)
        content += "<li><a href=\"" + rankings[r].Filename + "\" target=\"_blank\">" +
            rankings[r].PageTitle + "</a></li>";

    content += "</ol>";

    if(rankings.length < matchingFileIndices.length)
        content += "<p>Omitted " + (matchingFileIndices.length - rankings.length) + " more results</p>";

    return content;
}
