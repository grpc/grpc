(function () {
  importScripts('lunr.min.js');

  var lunrIndex;

  var stopWords = null;
  var searchData = {};

  lunr.tokenizer.seperator = /[\s\-\.]+/;

  var stopWordsRequest = new XMLHttpRequest();
  stopWordsRequest.open('GET', '../search-stopwords.json');
  stopWordsRequest.onload = function () {
    if (this.status != 200) {
      return;
    }
    stopWords = JSON.parse(this.responseText);
    buildIndex();
  }
  stopWordsRequest.send();

  var searchDataRequest = new XMLHttpRequest();

  searchDataRequest.open('GET', '../index.json');
  searchDataRequest.onload = function () {
    if (this.status != 200) {
      return;
    }
    searchData = JSON.parse(this.responseText);

    buildIndex();

    postMessage({ e: 'index-ready' });
  }
  searchDataRequest.send();

  onmessage = function (oEvent) {
    var q = oEvent.data.q;
    var hits = lunrIndex.search(q);
    var results = [];
    hits.forEach(function (hit) {
      var item = searchData[hit.ref];
      results.push({ 'href': item.href, 'title': item.title, 'keywords': item.keywords });
    });
    postMessage({ e: 'query-ready', q: q, d: results });
  }

  function buildIndex() {
    if (stopWords !== null && !isEmpty(searchData)) {
      lunrIndex = lunr(function () {
        this.pipeline.remove(lunr.stopWordFilter);
        this.ref('href');
        this.field('title', { boost: 50 });
        this.field('keywords', { boost: 20 });

        for (var prop in searchData) {
          if (searchData.hasOwnProperty(prop)) {
            this.add(searchData[prop]);
          }
        }

        var docfxStopWordFilter = lunr.generateStopWordFilter(stopWords);
        lunr.Pipeline.registerFunction(docfxStopWordFilter, 'docfxStopWordFilter');
        this.pipeline.add(docfxStopWordFilter);
        this.searchPipeline.add(docfxStopWordFilter);
      });
    }
  }

  function isEmpty(obj) {
    if(!obj) return true;

    for (var prop in obj) {
      if (obj.hasOwnProperty(prop))
        return false;
    }

    return true;
  }
})();
