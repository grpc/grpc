// Copyright (c) Microsoft. All rights reserved. Licensed under the MIT license. See LICENSE file in the project root for full license information.
$(function () {
  var active = 'active';
  var expanded = 'in';
  var collapsed = 'collapsed';
  var filtered = 'filtered';
  var show = 'show';
  var hide = 'hide';
  var util = new utility();

  workAroundFixedHeaderForAnchors();
  highlight();
  enableSearch();

  renderTables();
  renderAlerts();
  renderLinks();
  renderNavbar();
  renderSidebar();
  renderAffix();
  renderFooter();
  renderLogo();

  breakText();
  renderTabs();

  window.refresh = function (article) {
    // Update markup result
    if (typeof article == 'undefined' || typeof article.content == 'undefined')
      console.error("Null Argument");
    $("article.content").html(article.content);

    highlight();
    renderTables();
    renderAlerts();
    renderAffix();
    renderTabs();
  }

  // Add this event listener when needed
  // window.addEventListener('content-update', contentUpdate);

  function breakText() {
    $(".xref").addClass("text-break");
    var texts = $(".text-break");
    texts.each(function () {
      $(this).breakWord();
    });
  }

  // Styling for tables in conceptual documents using Bootstrap.
  // See http://getbootstrap.com/css/#tables
  function renderTables() {
    $('table').addClass('table table-bordered table-striped table-condensed').wrap('<div class=\"table-responsive\"></div>');
  }

  // Styling for alerts.
  function renderAlerts() {
    $('.NOTE, .TIP').addClass('alert alert-info');
    $('.WARNING').addClass('alert alert-warning');
    $('.IMPORTANT, .CAUTION').addClass('alert alert-danger');
  }

  // Enable anchors for headings.
  (function () {
    anchors.options = {
      placement: 'left',
      visible: 'touch'
    };
    anchors.add('article h2:not(.no-anchor), article h3:not(.no-anchor), article h4:not(.no-anchor)');
  })();

  // Open links to different host in a new window.
  function renderLinks() {
    if ($("meta[property='docfx:newtab']").attr("content") === "true") {
      $(document.links).filter(function () {
        return this.hostname !== window.location.hostname;
      }).attr('target', '_blank');
    }
  }

  // Enable highlight.js
  function highlight() {
    $('pre code').each(function (i, block) {
      hljs.highlightBlock(block);
    });
    $('pre code[highlight-lines]').each(function (i, block) {
      if (block.innerHTML === "") return;
      var lines = block.innerHTML.split('\n');

      queryString = block.getAttribute('highlight-lines');
      if (!queryString) return;

      var ranges = queryString.split(',');
      for (var j = 0, range; range = ranges[j++];) {
        var found = range.match(/^(\d+)\-(\d+)?$/);
        if (found) {
          // consider region as `{startlinenumber}-{endlinenumber}`, in which {endlinenumber} is optional
          var start = +found[1];
          var end = +found[2];
          if (isNaN(end) || end > lines.length) {
            end = lines.length;
          }
        } else {
          // consider region as a sigine line number
          if (isNaN(range)) continue;
          var start = +range;
          var end = start;
        }
        if (start <= 0 || end <= 0 || start > end || start > lines.length) {
          // skip current region if invalid
          continue;
        }
        lines[start - 1] = '<span class="line-highlight">' + lines[start - 1];
        lines[end - 1] = lines[end - 1] + '</span>';
      }

      block.innerHTML = lines.join('\n');
    });
  }

  // Support full-text-search
  function enableSearch() {
    var query;
    var relHref = $("meta[property='docfx\\:rel']").attr("content");
    if (typeof relHref === 'undefined') {
      return;
    }
    try {
      var worker = new Worker(relHref + 'styles/search-worker.js');
      if (!worker && !window.worker) {
        localSearch();
      } else {
        webWorkerSearch();
      }

      renderSearchBox();
      highlightKeywords();
      addSearchEvent();
    } catch (e) {
      console.error(e);
    }

    //Adjust the position of search box in navbar
    function renderSearchBox() {
      autoCollapse();
      $(window).on('resize', autoCollapse);
      $(document).on('click', '.navbar-collapse.in', function (e) {
        if ($(e.target).is('a')) {
          $(this).collapse('hide');
        }
      });

      function autoCollapse() {
        var navbar = $('#autocollapse');
        if (navbar.height() === null) {
          setTimeout(autoCollapse, 300);
        }
        navbar.removeClass(collapsed);
        if (navbar.height() > 60) {
          navbar.addClass(collapsed);
        }
      }
    }

    // Search factory
    function localSearch() {
      console.log("using local search");
      var lunrIndex = lunr(function () {
        this.ref('href');
        this.field('title', { boost: 50 });
        this.field('keywords', { boost: 20 });
      });
      lunr.tokenizer.seperator = /[\s\-\.]+/;
      var searchData = {};
      var searchDataRequest = new XMLHttpRequest();

      var indexPath = relHref + "index.json";
      if (indexPath) {
        searchDataRequest.open('GET', indexPath);
        searchDataRequest.onload = function () {
          if (this.status != 200) {
            return;
          }
          searchData = JSON.parse(this.responseText);
          for (var prop in searchData) {
            if (searchData.hasOwnProperty(prop)) {
              lunrIndex.add(searchData[prop]);
            }
          }
        }
        searchDataRequest.send();
      }

      $("body").bind("queryReady", function () {
        var hits = lunrIndex.search(query);
        var results = [];
        hits.forEach(function (hit) {
          var item = searchData[hit.ref];
          results.push({ 'href': item.href, 'title': item.title, 'keywords': item.keywords });
        });
        handleSearchResults(results);
      });
    }

    function webWorkerSearch() {
      console.log("using Web Worker");
      var indexReady = $.Deferred();

      worker.onmessage = function (oEvent) {
        switch (oEvent.data.e) {
          case 'index-ready':
            indexReady.resolve();
            break;
          case 'query-ready':
            var hits = oEvent.data.d;
            handleSearchResults(hits);
            break;
        }
      }

      indexReady.promise().done(function () {
        $("body").bind("queryReady", function () {
          worker.postMessage({ q: query });
        });
        if (query && (query.length >= 3)) {
          worker.postMessage({ q: query });
        }
      });
    }

    // Highlight the searching keywords
    function highlightKeywords() {
      var q = url('?q');
      if (q !== null) {
        var keywords = q.split("%20");
        keywords.forEach(function (keyword) {
          if (keyword !== "") {
            $('.data-searchable *').mark(keyword);
            $('article *').mark(keyword);
          }
        });
      }
    }

    function addSearchEvent() {
      $('body').bind("searchEvent", function () {
        $('#search-query').keypress(function (e) {
          return e.which !== 13;
        });

        $('#search-query').keyup(function () {
          query = $(this).val();
          if (query.length < 3) {
            flipContents("show");
          } else {
            flipContents("hide");
            $("body").trigger("queryReady");
            $('#search-results>.search-list').text('Search Results for "' + query + '"');
          }
        }).off("keydown");
      });
    }

    function flipContents(action) {
      if (action === "show") {
        $('.hide-when-search').show();
        $('#search-results').hide();
      } else {
        $('.hide-when-search').hide();
        $('#search-results').show();
      }
    }

    function relativeUrlToAbsoluteUrl(currentUrl, relativeUrl) {
      var currentItems = currentUrl.split(/\/+/);
      var relativeItems = relativeUrl.split(/\/+/);
      var depth = currentItems.length - 1;
      var items = [];
      for (var i = 0; i < relativeItems.length; i++) {
        if (relativeItems[i] === '..') {
          depth--;
        } else if (relativeItems[i] !== '.') {
          items.push(relativeItems[i]);
        }
      }
      return currentItems.slice(0, depth).concat(items).join('/');
    }

    function extractContentBrief(content) {
      var briefOffset = 512;
      var words = query.split(/\s+/g);
      var queryIndex = content.indexOf(words[0]);
      var briefContent;
      if (queryIndex > briefOffset) {
        return "..." + content.slice(queryIndex - briefOffset, queryIndex + briefOffset) + "...";
      } else if (queryIndex <= briefOffset) {
        return content.slice(0, queryIndex + briefOffset) + "...";
      }
    }

    function handleSearchResults(hits) {
      var numPerPage = 10;
      $('#pagination').empty();
      $('#pagination').removeData("twbs-pagination");
      if (hits.length === 0) {
        $('#search-results>.sr-items').html('<p>No results found</p>');
      } else {
        $('#pagination').twbsPagination({
          totalPages: Math.ceil(hits.length / numPerPage),
          visiblePages: 5,
          onPageClick: function (event, page) {
            var start = (page - 1) * numPerPage;
            var curHits = hits.slice(start, start + numPerPage);
            $('#search-results>.sr-items').empty().append(
              curHits.map(function (hit) {
                var currentUrl = window.location.href;
                var itemRawHref = relativeUrlToAbsoluteUrl(currentUrl, relHref + hit.href);
                var itemHref = relHref + hit.href + "?q=" + query;
                var itemTitle = hit.title;
                var itemBrief = extractContentBrief(hit.keywords);

                var itemNode = $('<div>').attr('class', 'sr-item');
                var itemTitleNode = $('<div>').attr('class', 'item-title').append($('<a>').attr('href', itemHref).attr("target", "_blank").text(itemTitle));
                var itemHrefNode = $('<div>').attr('class', 'item-href').text(itemRawHref);
                var itemBriefNode = $('<div>').attr('class', 'item-brief').text(itemBrief);
                itemNode.append(itemTitleNode).append(itemHrefNode).append(itemBriefNode);
                return itemNode;
              })
            );
            query.split(/\s+/).forEach(function (word) {
              if (word !== '') {
                $('#search-results>.sr-items *').mark(word);
              }
            });
          }
        });
      }
    }
  };

  // Update href in navbar
  function renderNavbar() {
    var navbar = $('#navbar ul')[0];
    if (typeof (navbar) === 'undefined') {
      loadNavbar();
    } else {
      $('#navbar ul a.active').parents('li').addClass(active);
      renderBreadcrumb();
    }

    function loadNavbar() {
      var navbarPath = $("meta[property='docfx\\:navrel']").attr("content");
      if (!navbarPath) {
        return;
      }
      navbarPath = navbarPath.replace(/\\/g, '/');
      var tocPath = $("meta[property='docfx\\:tocrel']").attr("content") || '';
      if (tocPath) tocPath = tocPath.replace(/\\/g, '/');
      $.get(navbarPath, function (data) {
        $(data).find("#toc>ul").appendTo("#navbar");
        if ($('#search-results').length !== 0) {
          $('#search').show();
          $('body').trigger("searchEvent");
        }
        var index = navbarPath.lastIndexOf('/');
        var navrel = '';
        if (index > -1) {
          navrel = navbarPath.substr(0, index + 1);
        }
        $('#navbar>ul').addClass('navbar-nav');
        var currentAbsPath = util.getAbsolutePath(window.location.pathname);
        // set active item
        $('#navbar').find('a[href]').each(function (i, e) {
          var href = $(e).attr("href");
          if (util.isRelativePath(href)) {
            href = navrel + href;
            $(e).attr("href", href);

            // TODO: currently only support one level navbar
            var isActive = false;
            var originalHref = e.name;
            if (originalHref) {
              originalHref = navrel + originalHref;
              if (util.getDirectory(util.getAbsolutePath(originalHref)) === util.getDirectory(util.getAbsolutePath(tocPath))) {
                isActive = true;
              }
            } else {
              if (util.getAbsolutePath(href) === currentAbsPath) {
                isActive = true;
              }
            }
            if (isActive) {
              $(e).addClass(active);
            }
          }
        });
        renderNavbar();
      });
    }
  }

  function renderSidebar() {
    var sidetoc = $('#sidetoggle .sidetoc')[0];
    if (typeof (sidetoc) === 'undefined') {
      loadToc();
    } else {
      registerTocEvents();
      if ($('footer').is(':visible')) {
        $('.sidetoc').addClass('shiftup');
      }

      // Scroll to active item
      var top = 0;
      $('#toc a.active').parents('li').each(function (i, e) {
        $(e).addClass(active).addClass(expanded);
        $(e).children('a').addClass(active);
        top += $(e).position().top;
      })
      $('.sidetoc').scrollTop(top - 50);

      if ($('footer').is(':visible')) {
        $('.sidetoc').addClass('shiftup');
      }

      renderBreadcrumb();
    }

    function registerTocEvents() {
      $('.toc .nav > li > .expand-stub').click(function (e) {
        $(e.target).parent().toggleClass(expanded);
      });
      $('.toc .nav > li > .expand-stub + a:not([href])').click(function (e) {
        $(e.target).parent().toggleClass(expanded);
      });
      $('#toc_filter_input').on('input', function (e) {
        var val = this.value;
        if (val === '') {
          // Clear 'filtered' class
          $('#toc li').removeClass(filtered).removeClass(hide);
          return;
        }

        // Get leaf nodes
        $('#toc li>a').filter(function (i, e) {
          return $(e).siblings().length === 0
        }).each(function (i, anchor) {
          var text = $(anchor).attr('title');
          var parent = $(anchor).parent();
          var parentNodes = parent.parents('ul>li');
          for (var i = 0; i < parentNodes.length; i++) {
            var parentText = $(parentNodes[i]).children('a').attr('title');
            if (parentText) text = parentText + '.' + text;
          };
          if (filterNavItem(text, val)) {
            parent.addClass(show);
            parent.removeClass(hide);
          } else {
            parent.addClass(hide);
            parent.removeClass(show);
          }
        });
        $('#toc li>a').filter(function (i, e) {
          return $(e).siblings().length > 0
        }).each(function (i, anchor) {
          var parent = $(anchor).parent();
          if (parent.find('li.show').length > 0) {
            parent.addClass(show);
            parent.addClass(filtered);
            parent.removeClass(hide);
          } else {
            parent.addClass(hide);
            parent.removeClass(show);
            parent.removeClass(filtered);
          }
        })

        function filterNavItem(name, text) {
          if (!text) return true;
          if (name && name.toLowerCase().indexOf(text.toLowerCase()) > -1) return true;
          return false;
        }
      });
    }

    function loadToc() {
      var tocPath = $("meta[property='docfx\\:tocrel']").attr("content");
      if (!tocPath) {
        return;
      }
      tocPath = tocPath.replace(/\\/g, '/');
      $('#sidetoc').load(tocPath + " #sidetoggle > div", function () {
        var index = tocPath.lastIndexOf('/');
        var tocrel = '';
        if (index > -1) {
          tocrel = tocPath.substr(0, index + 1);
        }
        var currentHref = util.getAbsolutePath(window.location.pathname);
        $('#sidetoc').find('a[href]').each(function (i, e) {
          var href = $(e).attr("href");
          if (util.isRelativePath(href)) {
            href = tocrel + href;
            $(e).attr("href", href);
          }

          if (util.getAbsolutePath(e.href) === currentHref) {
            $(e).addClass(active);
          }

          $(e).breakWord();
        });

        renderSidebar();
      });
    }
  }

  function renderBreadcrumb() {
    var breadcrumb = [];
    $('#navbar a.active').each(function (i, e) {
      breadcrumb.push({
        href: e.href,
        name: e.innerHTML
      });
    })
    $('#toc a.active').each(function (i, e) {
      breadcrumb.push({
        href: e.href,
        name: e.innerHTML
      });
    })

    var html = util.formList(breadcrumb, 'breadcrumb');
    $('#breadcrumb').html(html);
  }

  //Setup Affix
  function renderAffix() {
    var hierarchy = getHierarchy();
    if (hierarchy && hierarchy.length > 0) {
      var html = '<h5 class="title">In This Article</h5>'
      html += util.formList(hierarchy, ['nav', 'bs-docs-sidenav']);
      $("#affix").empty().append(html);
      if ($('footer').is(':visible')) {
        $(".sideaffix").css("bottom", "70px");
      }
      $('#affix a').click(function() {
        var scrollspy = $('[data-spy="scroll"]').data()['bs.scrollspy'];
        var target = e.target.hash;
        if (scrollspy && target) {
          scrollspy.activate(target);
        }
      });
    }

    function getHierarchy() {
      // supported headers are h1, h2, h3, and h4
      var $headers = $($.map(['h1', 'h2', 'h3', 'h4'], function (h) { return ".article article " + h; }).join(", "));

      // a stack of hierarchy items that are currently being built
      var stack = [];
      $headers.each(function (i, e) {
        if (!e.id) {
          return;
        }

        var item = {
          name: htmlEncode($(e).text()),
          href: "#" + e.id,
          items: []
        };

        if (!stack.length) {
          stack.push({ type: e.tagName, siblings: [item] });
          return;
        }

        var frame = stack[stack.length - 1];
        if (e.tagName === frame.type) {
          frame.siblings.push(item);
        } else if (e.tagName[1] > frame.type[1]) {
          // we are looking at a child of the last element of frame.siblings.
          // push a frame onto the stack. After we've finished building this item's children,
          // we'll attach it as a child of the last element
          stack.push({ type: e.tagName, siblings: [item] });
        } else {  // e.tagName[1] < frame.type[1]
          // we are looking at a sibling of an ancestor of the current item.
          // pop frames from the stack, building items as we go, until we reach the correct level at which to attach this item.
          while (e.tagName[1] < stack[stack.length - 1].type[1]) {
            buildParent();
          }
          if (e.tagName === stack[stack.length - 1].type) {
            stack[stack.length - 1].siblings.push(item);
          } else {
            stack.push({ type: e.tagName, siblings: [item] });
          }
        }
      });
      while (stack.length > 1) {
        buildParent();
      }

      function buildParent() {
        var childrenToAttach = stack.pop();
        var parentFrame = stack[stack.length - 1];
        var parent = parentFrame.siblings[parentFrame.siblings.length - 1];
        $.each(childrenToAttach.siblings, function (i, child) {
          parent.items.push(child);
        });
      }
      if (stack.length > 0) {

        var topLevel = stack.pop().siblings;
        if (topLevel.length === 1) {  // if there's only one topmost header, dump it
          return topLevel[0].items;
        }
        return topLevel;
      }
      return undefined;
    }

    function htmlEncode(str) {
      if (!str) return str;
      return str
        .replace(/&/g, '&amp;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
    }

    function htmlDecode(value) {
      if (!str) return str;
      return value
        .replace(/&quot;/g, '"')
        .replace(/&#39;/g, "'")
        .replace(/&lt;/g, '<')
        .replace(/&gt;/g, '>')
        .replace(/&amp;/g, '&');
    }

    function cssEscape(str) {
      // see: http://stackoverflow.com/questions/2786538/need-to-escape-a-special-character-in-a-jquery-selector-string#answer-2837646
      if (!str) return str;
      return str
        .replace(/[!"#$%&'()*+,.\/:;<=>?@[\\\]^`{|}~]/g, "\\$&");
    }
  }

  // Show footer
  function renderFooter() {
    initFooter();
    $(window).on("scroll", showFooterCore);

    function initFooter() {
      if (needFooter()) {
        shiftUpBottomCss();
        $("footer").show();
      } else {
        resetBottomCss();
        $("footer").hide();
      }
    }

    function showFooterCore() {
      if (needFooter()) {
        shiftUpBottomCss();
        $("footer").fadeIn();
      } else {
        resetBottomCss();
        $("footer").fadeOut();
      }
    }

    function needFooter() {
      var scrollHeight = $(document).height();
      var scrollPosition = $(window).height() + $(window).scrollTop();
      return (scrollHeight - scrollPosition) < 1;
    }

    function resetBottomCss() {
      $(".sidetoc").removeClass("shiftup");
      $(".sideaffix").removeClass("shiftup");
    }

    function shiftUpBottomCss() {
      $(".sidetoc").addClass("shiftup");
      $(".sideaffix").addClass("shiftup");
    }
  }

  function renderLogo() {
    // For LOGO SVG
    // Replace SVG with inline SVG
    // http://stackoverflow.com/questions/11978995/how-to-change-color-of-svg-image-using-css-jquery-svg-image-replacement
    jQuery('img.svg').each(function () {
      var $img = jQuery(this);
      var imgID = $img.attr('id');
      var imgClass = $img.attr('class');
      var imgURL = $img.attr('src');

      jQuery.get(imgURL, function (data) {
        // Get the SVG tag, ignore the rest
        var $svg = jQuery(data).find('svg');

        // Add replaced image's ID to the new SVG
        if (typeof imgID !== 'undefined') {
          $svg = $svg.attr('id', imgID);
        }
        // Add replaced image's classes to the new SVG
        if (typeof imgClass !== 'undefined') {
          $svg = $svg.attr('class', imgClass + ' replaced-svg');
        }

        // Remove any invalid XML tags as per http://validator.w3.org
        $svg = $svg.removeAttr('xmlns:a');

        // Replace image with new SVG
        $img.replaceWith($svg);

      }, 'xml');
    });
  }

  function renderTabs() {
    var contentAttrs = {
      id: 'data-bi-id',
      name: 'data-bi-name',
      type: 'data-bi-type'
    };

    var Tab = (function () {
      function Tab(li, a, section) {
        this.li = li;
        this.a = a;
        this.section = section;
      }
      Object.defineProperty(Tab.prototype, "tabIds", {
        get: function () { return this.a.getAttribute('data-tab').split(' '); },
        enumerable: true,
        configurable: true
      });
      Object.defineProperty(Tab.prototype, "condition", {
        get: function () { return this.a.getAttribute('data-condition'); },
        enumerable: true,
        configurable: true
      });
      Object.defineProperty(Tab.prototype, "visible", {
        get: function () { return !this.li.hasAttribute('hidden'); },
        set: function (value) {
          if (value) {
            this.li.removeAttribute('hidden');
            this.li.removeAttribute('aria-hidden');
          }
          else {
            this.li.setAttribute('hidden', 'hidden');
            this.li.setAttribute('aria-hidden', 'true');
          }
        },
        enumerable: true,
        configurable: true
      });
      Object.defineProperty(Tab.prototype, "selected", {
        get: function () { return !this.section.hasAttribute('hidden'); },
        set: function (value) {
          if (value) {
            this.a.setAttribute('aria-selected', 'true');
            this.a.tabIndex = 0;
            this.section.removeAttribute('hidden');
            this.section.removeAttribute('aria-hidden');
          }
          else {
            this.a.setAttribute('aria-selected', 'false');
            this.a.tabIndex = -1;
            this.section.setAttribute('hidden', 'hidden');
            this.section.setAttribute('aria-hidden', 'true');
          }
        },
        enumerable: true,
        configurable: true
      });
      Tab.prototype.focus = function () {
        this.a.focus();
      };
      return Tab;
    }());

    initTabs(document.body);

    function initTabs(container) {
      var queryStringTabs = readTabsQueryStringParam();
      var elements = container.querySelectorAll('.tabGroup');
      var state = { groups: [], selectedTabs: [] };
      for (var i = 0; i < elements.length; i++) {
        var group = initTabGroup(elements.item(i));
        if (!group.independent) {
          updateVisibilityAndSelection(group, state);
          state.groups.push(group);
        }
      }
      container.addEventListener('click', function (event) { return handleClick(event, state); });
      if (state.groups.length === 0) {
        return state;
      }
      selectTabs(queryStringTabs, container);
      updateTabsQueryStringParam(state);
      notifyContentUpdated();
      return state;
    }

    function initTabGroup(element) {
      var group = {
        independent: element.hasAttribute('data-tab-group-independent'),
        tabs: []
      };
      var li = element.firstElementChild.firstElementChild;
      while (li) {
        var a = li.firstElementChild;
        a.setAttribute(contentAttrs.name, 'tab');
        var dataTab = a.getAttribute('data-tab').replace(/\+/g, ' ');
        a.setAttribute('data-tab', dataTab);
        var section = element.querySelector("[id=\"" + a.getAttribute('aria-controls') + "\"]");
        var tab = new Tab(li, a, section);
        group.tabs.push(tab);
        li = li.nextElementSibling;
      }
      element.setAttribute(contentAttrs.name, 'tab-group');
      element.tabGroup = group;
      return group;
    }

    function updateVisibilityAndSelection(group, state) {
      var anySelected = false;
      var firstVisibleTab;
      for (var _i = 0, _a = group.tabs; _i < _a.length; _i++) {
        var tab = _a[_i];
        tab.visible = tab.condition === null || state.selectedTabs.indexOf(tab.condition) !== -1;
        if (tab.visible) {
          if (!firstVisibleTab) {
            firstVisibleTab = tab;
          }
        }
        tab.selected = tab.visible && arraysIntersect(state.selectedTabs, tab.tabIds);
        anySelected = anySelected || tab.selected;
      }
      if (!anySelected) {
        for (var _b = 0, _c = group.tabs; _b < _c.length; _b++) {
          var tabIds = _c[_b].tabIds;
          for (var _d = 0, tabIds_1 = tabIds; _d < tabIds_1.length; _d++) {
            var tabId = tabIds_1[_d];
            var index = state.selectedTabs.indexOf(tabId);
            if (index === -1) {
              continue;
            }
            state.selectedTabs.splice(index, 1);
          }
        }
        var tab = firstVisibleTab;
        tab.selected = true;
        state.selectedTabs.push(tab.tabIds[0]);
      }
    }

    function getTabInfoFromEvent(event) {
      if (!(event.target instanceof HTMLElement)) {
        return null;
      }
      var anchor = event.target.closest('a[data-tab]');
      if (anchor === null) {
        return null;
      }
      var tabIds = anchor.getAttribute('data-tab').split(' ');
      var group = anchor.parentElement.parentElement.parentElement.tabGroup;
      if (group === undefined) {
        return null;
      }
      return { tabIds: tabIds, group: group, anchor: anchor };
    }

    function handleClick(event, state) {
      var info = getTabInfoFromEvent(event);
      if (info === null) {
        return;
      }
      event.preventDefault();
      info.anchor.href = 'javascript:';
      setTimeout(function () { return info.anchor.href = '#' + info.anchor.getAttribute('aria-controls'); });
      var tabIds = info.tabIds, group = info.group;
      var originalTop = info.anchor.getBoundingClientRect().top;
      if (group.independent) {
        for (var _i = 0, _a = group.tabs; _i < _a.length; _i++) {
          var tab = _a[_i];
          tab.selected = arraysIntersect(tab.tabIds, tabIds);
        }
      }
      else {
        if (arraysIntersect(state.selectedTabs, tabIds)) {
          return;
        }
        var previousTabId = group.tabs.filter(function (t) { return t.selected; })[0].tabIds[0];
        state.selectedTabs.splice(state.selectedTabs.indexOf(previousTabId), 1, tabIds[0]);
        for (var _b = 0, _c = state.groups; _b < _c.length; _b++) {
          var group_1 = _c[_b];
          updateVisibilityAndSelection(group_1, state);
        }
        updateTabsQueryStringParam(state);
      }
      notifyContentUpdated();
      var top = info.anchor.getBoundingClientRect().top;
      if (top !== originalTop && event instanceof MouseEvent) {
        window.scrollTo(0, window.pageYOffset + top - originalTop);
      }
    }

    function selectTabs(tabIds) {
      for (var _i = 0, tabIds_1 = tabIds; _i < tabIds_1.length; _i++) {
        var tabId = tabIds_1[_i];
        var a = document.querySelector(".tabGroup > ul > li > a[data-tab=\"" + tabId + "\"]:not([hidden])");
        if (a === null) {
          return;
        }
        a.dispatchEvent(new CustomEvent('click', { bubbles: true }));
      }
    }

    function readTabsQueryStringParam() {
      var qs = parseQueryString();
      var t = qs.tabs;
      if (t === undefined || t === '') {
        return [];
      }
      return t.split(',');
    }

    function updateTabsQueryStringParam(state) {
      var qs = parseQueryString();
      qs.tabs = state.selectedTabs.join();
      var url = location.protocol + "//" + location.host + location.pathname + "?" + toQueryString(qs) + location.hash;
      if (location.href === url) {
        return;
      }
      history.replaceState({}, document.title, url);
    }

    function toQueryString(args) {
      var parts = [];
      for (var name_1 in args) {
        if (args.hasOwnProperty(name_1) && args[name_1] !== '' && args[name_1] !== null && args[name_1] !== undefined) {
          parts.push(encodeURIComponent(name_1) + '=' + encodeURIComponent(args[name_1]));
        }
      }
      return parts.join('&');
    }

    function parseQueryString(queryString) {
      var match;
      var pl = /\+/g;
      var search = /([^&=]+)=?([^&]*)/g;
      var decode = function (s) { return decodeURIComponent(s.replace(pl, ' ')); };
      if (queryString === undefined) {
        queryString = '';
      }
      queryString = queryString.substring(1);
      var urlParams = {};
      while (match = search.exec(queryString)) {
        urlParams[decode(match[1])] = decode(match[2]);
      }
      return urlParams;
    }

    function arraysIntersect(a, b) {
      for (var _i = 0, a_1 = a; _i < a_1.length; _i++) {
        var itemA = a_1[_i];
        for (var _a = 0, b_1 = b; _a < b_1.length; _a++) {
          var itemB = b_1[_a];
          if (itemA === itemB) {
            return true;
          }
        }
      }
      return false;
    }

    function notifyContentUpdated() {
      // Dispatch this event when needed
      // window.dispatchEvent(new CustomEvent('content-update'));
    }
  }

  function utility() {
    this.getAbsolutePath = getAbsolutePath;
    this.isRelativePath = isRelativePath;
    this.isAbsolutePath = isAbsolutePath;
    this.getDirectory = getDirectory;
    this.formList = formList;

    function getAbsolutePath(href) {
      // Use anchor to normalize href
      var anchor = $('<a href="' + href + '"></a>')[0];
      // Ignore protocal, remove search and query
      return anchor.host + anchor.pathname;
    }

    function isRelativePath(href) {
      if (href === undefined || href === '' || href[0] === '/') {
        return false;
      }
      return !isAbsolutePath(href);
    }

    function isAbsolutePath(href) {
      return (/^(?:[a-z]+:)?\/\//i).test(href);
    }

    function getDirectory(href) {
      if (!href) return '';
      var index = href.lastIndexOf('/');
      if (index == -1) return '';
      if (index > -1) {
        return href.substr(0, index);
      }
    }

    function formList(item, classes) {
      var level = 1;
      var model = {
        items: item
      };
      var cls = [].concat(classes).join(" ");
      return getList(model, cls);

      function getList(model, cls) {
        if (!model || !model.items) return null;
        var l = model.items.length;
        if (l === 0) return null;
        var html = '<ul class="level' + level + ' ' + (cls || '') + '">';
        level++;
        for (var i = 0; i < l; i++) {
          var item = model.items[i];
          var href = item.href;
          var name = item.name;
          if (!name) continue;
          html += href ? '<li><a href="' + href + '">' + name + '</a>' : '<li>' + name;
          html += getList(item, cls) || '';
          html += '</li>';
        }
        html += '</ul>';
        return html;
      }
    }

    /**
     * Add <wbr> into long word.
     * @param {String} text - The word to break. It should be in plain text without HTML tags.
     */
    function breakPlainText(text) {
      if (!text) return text;
      return text.replace(/([a-z])([A-Z])|(\.)(\w)/g, '$1$3<wbr>$2$4')
    }

    /**
     * Add <wbr> into long word. The jQuery element should contain no html tags.
     * If the jQuery element contains tags, this function will not change the element.
     */
    $.fn.breakWord = function () {
      if (this.html() == this.text()) {
        this.html(function (index, text) {
          return breakPlainText(text);
        })
      }
      return this;
    }
  }

  // adjusted from https://stackoverflow.com/a/13067009/1523776
  function workAroundFixedHeaderForAnchors() {
    var HISTORY_SUPPORT = !!(history && history.pushState);
    var ANCHOR_REGEX = /^#[^ ]+$/;

    function getFixedOffset() {
      return $('header').first().height();
    }

    /**
     * If the provided href is an anchor which resolves to an element on the
     * page, scroll to it.
     * @param  {String} href
     * @return {Boolean} - Was the href an anchor.
     */
    function scrollIfAnchor(href, pushToHistory) {
      var match, rect, anchorOffset;

      if (!ANCHOR_REGEX.test(href)) {
        return false;
      }

      match = document.getElementById(href.slice(1));

      if (match) {
        rect = match.getBoundingClientRect();
        anchorOffset = window.pageYOffset + rect.top - getFixedOffset();
        window.scrollTo(window.pageXOffset, anchorOffset);

        // Add the state to history as-per normal anchor links
        if (HISTORY_SUPPORT && pushToHistory) {
          history.pushState({}, document.title, location.pathname + href);
        }
      }

      return !!match;
    }

    /**
     * Attempt to scroll to the current location's hash.
     */
    function scrollToCurrent() {
      scrollIfAnchor(window.location.hash);
    }

    /**
     * If the click event's target was an anchor, fix the scroll position.
     */
    function delegateAnchors(e) {
      var elem = e.target;

      if (scrollIfAnchor(elem.getAttribute('href'), true)) {
        e.preventDefault();
      }
    }

    $(window).on('hashchange', scrollToCurrent);
    // Exclude tabbed content case
    $('a:not([data-tab])').click(delegateAnchors);
    scrollToCurrent();
  }
});
