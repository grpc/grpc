<?
// Contributed to the Sandcastle Help File Builder project by Thomas Levesque

class Ranking
{
    public $filename;
    public $pageTitle;
    public $rank;

    function __construct($file, $title, $rank)
    {
        $this->filename = $file;
        $this->pageTitle = $title;
        $this->rank = $rank;
    }
}


/// <summary>
/// Split the search text up into keywords
/// </summary>
/// <param name="keywords">The keywords to parse</param>
/// <returns>A list containing the words for which to search</returns>
function ParseKeywords($keywords)
{
    $keywordList = array();
    $words = preg_split("/[^\w]+/", $keywords);

    foreach($words as $word)
    {
        $checkWord = strtolower($word);
        $first = substr($checkWord, 0, 1);
        if(strlen($checkWord) > 2 && !ctype_digit($first) && !in_array($checkWord, $keywordList))
        {
            array_push($keywordList, $checkWord);
        }
    }

    return $keywordList;
}


/// <summary>
/// Search for the specified keywords and return the results as a block of
/// HTML.
/// </summary>
/// <param name="keywords">The keywords for which to search</param>
/// <param name="fileInfo">The file list</param>
/// <param name="wordDictionary">The dictionary used to find the words</param>
/// <param name="sortByTitle">True to sort by title, false to sort by
/// ranking</param>
/// <returns>A block of HTML representing the search results.</returns>
function Search($keywords, $fileInfo, $wordDictionary, $sortByTitle)
{
    $sb = "<ol>";
    $matches = array();
    $matchingFileIndices = array();
    $rankings = array();

    $isFirst = true;

    foreach($keywords as $word)
    {
        if (!array_key_exists($word, $wordDictionary))
        {
            return "<strong>Nothing found</strong>";
        }
        $occurrences = $wordDictionary[$word];

        $matches[$word] = $occurrences;
        $occurrenceIndices = array();

        // Get a list of the file indices for this match
        foreach($occurrences as $entry)
            array_push($occurrenceIndices, ($entry >> 16));

        if($isFirst)
        {
            $isFirst = false;
            foreach($occurrenceIndices as $i)
            {
                array_push($matchingFileIndices, $i);
            }
        }
        else
        {
            // After the first match, remove files that do not appear for
            // all found keywords.
            for($idx = 0; $idx < count($matchingFileIndices); $idx++)
            {
                if (!in_array($matchingFileIndices[$idx], $occurrenceIndices))
                {
                    array_splice($matchingFileIndices, $idx, 1);
                    $idx--;
                }
            }
        }
    }

    if(count($matchingFileIndices) == 0)
    {
        return "<strong>Nothing found</strong>";
    }

    // Rank the files based on the number of times the words occurs
    foreach($matchingFileIndices as $index)
    {
        // Split out the title, filename, and word count
        $fileIndex = explode("\x00", $fileInfo[$index]);

        $title = $fileIndex[0];
        $filename = $fileIndex[1];
        $wordCount = intval($fileIndex[2]);
        $matchCount = 0;

        foreach($keywords as $words)
        {
            $occurrences = $matches[$word];

            foreach($occurrences as $entry)
            {
                if(($entry >> 16) == $index)
                    $matchCount += $entry & 0xFFFF;
            }
        }

        $r = new Ranking($filename, $title, $matchCount * 1000 / $wordCount);
        array_push($rankings, $r);

        if(count($rankings) > 99)
            break;
    }

    // Sort by rank in descending order or by page title in ascending order
    if($sortByTitle)
    {
        usort($rankings, "cmprankbytitle");
    }
    else
    {
        usort($rankings, "cmprank");
    }

    // Format the file list and return the results
    foreach($rankings as $r)
    {
        $f = $r->filename;
        $t = $r->pageTitle;
        $sb .= "<li><a href=\"$f\" target=\"_blank\">$t</a></li>";
    }

    $sb .= "</ol";

    if(count($rankings) < count($matchingFileIndices))
    {
        $c = count(matchingFileIndices) - count(rankings);
        $sb .= "<p>Omitted $c more results</p>";
    }

    return $sb;
}

function cmprank($x, $y)
{
    return $y->rank - $x->rank;
}

function cmprankbytitle($x, $y)
{
    return strcmp($x->pageTitle, $y->pageTitle);
}

?>
