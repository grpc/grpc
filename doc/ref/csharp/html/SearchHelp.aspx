<%@ Page Language="C#" EnableViewState="False" %>

<script runat="server">
//===============================================================================================================
// System  : Sandcastle Help File Builder
// File    : SearchHelp.aspx
// Author  : Eric Woodruff  (Eric@EWoodruff.us)
// Updated : 05/15/2014
// Note    : Copyright 2007-2015, Eric Woodruff, All rights reserved
// Compiler: Microsoft C#
//
// This file contains the code used to search for keywords within the help topics using the full-text index
// files created by the help file builder.
//
// This code is published under the Microsoft Public License (Ms-PL).  A copy of the license should be
// distributed with the code.  It can also be found at the project website: https://GitHub.com/EWSoftware/SHFB.  This
// notice, the author's name, and all copyright notices must remain intact in all applications, documentation,
// and source files.
//
//    Date     Who  Comments
// ==============================================================================================================
// 06/24/2007  EFW  Created the code
// 02/17/2012  EFW  Switched to JSON serialization to support websites that use something other than ASP.NET
//                  such as PHP.
// 05/15/2014  EFW  Updated for use with the lightweight website presentation styles
//===============================================================================================================

/// <summary>
/// This class is used to track the results and their rankings
/// </summary>
private class Ranking
{
    public string Filename, PageTitle;
    public int Rank;

    public Ranking(string file, string title, int rank)
    {
        Filename = file;
        PageTitle = title;
        Rank = rank;
    }
}

/// <summary>
/// Render the search results
/// </summary>
/// <param name="writer">The writer to which the results are written</param>
protected override void Render(HtmlTextWriter writer)
{
    JavaScriptSerializer jss = new JavaScriptSerializer();
    string searchText, ftiFile;
    char letter;
    bool sortByTitle = false;

    jss.MaxJsonLength = Int32.MaxValue;

    // The keywords for which to search should be passed in the query string
    searchText = this.Request.QueryString["Keywords"];

    if(String.IsNullOrEmpty(searchText))
    {
        writer.Write("<strong>Nothing found</strong>");
        return;
    }

    // An optional SortByTitle option can also be specified
    if(this.Request.QueryString["SortByTitle"] != null)
        sortByTitle = Convert.ToBoolean(this.Request.QueryString["SortByTitle"]);

    List<string> keywords = this.ParseKeywords(searchText);
    List<char> letters = new List<char>();
    List<string> fileList;
    Dictionary<string, List<long>> ftiWords, wordDictionary = new Dictionary<string,List<long>>();

    // Load the file index
    using(StreamReader sr = new StreamReader(Server.MapPath("fti/FTI_Files.json")))
    {
        fileList = jss.Deserialize<List<string>>(sr.ReadToEnd());
    }

    // Load the required word index files
    foreach(string word in keywords)
    {
        letter = word[0];

        if(!letters.Contains(letter))
        {
            letters.Add(letter);
            ftiFile = Server.MapPath(String.Format(CultureInfo.InvariantCulture, "fti/FTI_{0}.json", (int)letter));

            if(File.Exists(ftiFile))
            {
                using(StreamReader sr = new StreamReader(ftiFile))
                {
                    ftiWords = jss.Deserialize<Dictionary<string, List<long>>>(sr.ReadToEnd());
                }

                foreach(string ftiWord in ftiWords.Keys)
                    wordDictionary.Add(ftiWord, ftiWords[ftiWord]);
            }
        }
    }

    // Perform the search and return the results as a block of HTML
    writer.Write(this.Search(keywords, fileList, wordDictionary, sortByTitle));
}

/// <summary>
/// Split the search text up into keywords
/// </summary>
/// <param name="keywords">The keywords to parse</param>
/// <returns>A list containing the words for which to search</returns>
private List<string> ParseKeywords(string keywords)
{
    List<string> keywordList = new List<string>();
    string checkWord;
    string[] words = Regex.Split(keywords, @"\W+");

    foreach(string word in words)
    {
        checkWord = word.ToLower(CultureInfo.InvariantCulture);
        
        if(checkWord.Length > 2 && !Char.IsDigit(checkWord[0]) && !keywordList.Contains(checkWord))
            keywordList.Add(checkWord);
    }

    return keywordList;
}

/// <summary>
/// Search for the specified keywords and return the results as a block of HTML
/// </summary>
/// <param name="keywords">The keywords for which to search</param>
/// <param name="fileInfo">The file list</param>
/// <param name="wordDictionary">The dictionary used to find the words</param>
/// <param name="sortByTitle">True to sort by title, false to sort by ranking</param>
/// <returns>A block of HTML representing the search results</returns>
private string Search(List<string> keywords, List<string> fileInfo,
  Dictionary<string, List<long>> wordDictionary, bool sortByTitle)
{
    StringBuilder sb = new StringBuilder(10240);
    Dictionary<string, List<long>> matches = new Dictionary<string, List<long>>();
    List<long> occurrences;
    List<int> matchingFileIndices = new List<int>(), occurrenceIndices = new List<int>();
    List<Ranking> rankings = new List<Ranking>();

    string filename, title;
    string[] fileIndex;
    bool isFirst = true;
    int idx, wordCount, matchCount;

    foreach(string word in keywords)
    {
        if(!wordDictionary.TryGetValue(word, out occurrences))
            return "<strong>Nothing found</strong>";

        matches.Add(word, occurrences);
        occurrenceIndices.Clear();

        // Get a list of the file indices for this match
        foreach(long entry in occurrences)
            occurrenceIndices.Add((int)(entry >> 16));
            
        if(isFirst)
        {
            isFirst = false;
            matchingFileIndices.AddRange(occurrenceIndices);
        }
        else
        {
            // After the first match, remove files that do not appear for
            // all found keywords.
            for(idx = 0; idx < matchingFileIndices.Count; idx++)
                if(!occurrenceIndices.Contains(matchingFileIndices[idx]))
                {
                    matchingFileIndices.RemoveAt(idx);
                    idx--;
                }
        }
    }

    if(matchingFileIndices.Count == 0)
        return "<strong>Nothing found</strong>";

    // Rank the files based on the number of times the words occurs
    foreach(int index in matchingFileIndices)
    {
        // Split out the title, filename, and word count
        fileIndex = fileInfo[index].Split('\x0');

        title = fileIndex[0];
        filename = fileIndex[1];
        wordCount = Convert.ToInt32(fileIndex[2]);
        matchCount = 0;
        
        foreach(string word in keywords)
        {
            occurrences = matches[word];

            foreach(long entry in occurrences)
                if((int)(entry >> 16) == index)
                    matchCount += (int)(entry & 0xFFFF);
        }

        rankings.Add(new Ranking(filename, title, matchCount * 1000 / wordCount));
		
        if(rankings.Count > 99)
            break;				
    }

    // Sort by rank in descending order or by page title in ascending order
    rankings.Sort(delegate (Ranking x, Ranking y)
    {
        if(!sortByTitle)
            return y.Rank - x.Rank;

        return x.PageTitle.CompareTo(y.PageTitle);
    });

    // Format the file list and return the results
		sb.Append("<ol>");

    foreach(Ranking r in rankings)
        sb.AppendFormat("<li><a href=\"{0}\" target=\"_blank\">{1}</a></li>", r.Filename, r.PageTitle);

		sb.Append("</ol>");

    if(rankings.Count < matchingFileIndices.Count)
        sb.AppendFormat("<p>Omitted {0} more results</p>", matchingFileIndices.Count - rankings.Count);

    return sb.ToString();
}
</script>
