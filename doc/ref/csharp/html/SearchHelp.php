<?
// Contributed to the Sandcastle Help File Builder project by Thomas Levesque

include("SearchHelp.inc.php");

    $sortByTitle = false;

    // The keywords for which to search should be passed in the query string
    $searchText = $_GET["Keywords"];

    if(empty($searchText))
    {
    ?>
        <strong>Nothing found</strong>
    <?
        return;
    }

    // An optional SortByTitle option can also be specified
    if($_GET["SortByTitle"] == "true")
        $sortByTitle = true;

    $keywords = ParseKeywords($searchText);
    $letters = array();
    $wordDictionary = array();

    // Load the file index
    $json = file_get_contents("fti/FTI_Files.json");
    $fileList = json_decode($json);

    // Load the required word index files
    foreach($keywords as $word)
    {
        $letter = substr($word, 0, 1);

        if(!in_array($letter, $letters))
        {
            array_push($letters, $letter);
            $ascii = ord($letter);
            $ftiFile = "fti/FTI_$ascii.json";

            if(file_exists($ftiFile))
            {
                $json = file_get_contents($ftiFile);
                $ftiWords = json_decode($json, true);

                foreach($ftiWords as $ftiWord => $val)
                {
                    $wordDictionary[$ftiWord] = $val;
                }
            }
        }
    }

    // Perform the search and return the results as a block of HTML
    $results = Search($keywords, $fileList, $wordDictionary, $sortByTitle);
    echo $results;
?>