/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
 */
function toggleVisibility(linkObj)
{
 var base = $(linkObj).attr('id');
 var summary = $('#'+base+'-summary');
 var content = $('#'+base+'-content');
 var trigger = $('#'+base+'-trigger');
 var src=$(trigger).attr('src');
 if (content.is(':visible')===true) {
   content.hide();
   summary.show();
   $(linkObj).addClass('closed').removeClass('opened');
   $(trigger).attr('src',src.substring(0,src.length-8)+'closed.png');
 } else {
   content.show();
   summary.hide();
   $(linkObj).removeClass('closed').addClass('opened');
   $(trigger).attr('src',src.substring(0,src.length-10)+'open.png');
 }
 return false;
}

function updateStripes()
{
  $('table.directory tr').
       removeClass('even').filter(':visible:even').addClass('even');
  $('table.directory tr').
       removeClass('odd').filter(':visible:odd').addClass('odd');
}

function toggleLevel(level)
{
  $('table.directory tr').each(function() {
    var l = this.id.split('_').length-1;
    var i = $('#img'+this.id.substring(3));
    var a = $('#arr'+this.id.substring(3));
    if (l<level+1) {
      i.removeClass('iconfopen iconfclosed').addClass('iconfopen');
      a.html('&#9660;');
      $(this).show();
    } else if (l==level+1) {
      i.removeClass('iconfclosed iconfopen').addClass('iconfclosed');
      a.html('&#9658;');
      $(this).show();
    } else {
      $(this).hide();
    }
  });
  updateStripes();
}

function toggleFolder(id)
{
  // the clicked row
  var currentRow = $('#row_'+id);

  // all rows after the clicked row
  var rows = currentRow.nextAll("tr");

  var re = new RegExp('^row_'+id+'\\d+_$', "i"); //only one sub

  // only match elements AFTER this one (can't hide elements before)
  var childRows = rows.filter(function() { return this.id.match(re); });

  // first row is visible we are HIDING
  if (childRows.filter(':first').is(':visible')===true) {
    // replace down arrow by right arrow for current row
    var currentRowSpans = currentRow.find("span");
    currentRowSpans.filter(".iconfopen").removeClass("iconfopen").addClass("iconfclosed");
    currentRowSpans.filter(".arrow").html('&#9658;');
    rows.filter("[id^=row_"+id+"]").hide(); // hide all children
  } else { // we are SHOWING
    // replace right arrow by down arrow for current row
    var currentRowSpans = currentRow.find("span");
    currentRowSpans.filter(".iconfclosed").removeClass("iconfclosed").addClass("iconfopen");
    currentRowSpans.filter(".arrow").html('&#9660;');
    // replace down arrows by right arrows for child rows
    var childRowsSpans = childRows.find("span");
    childRowsSpans.filter(".iconfopen").removeClass("iconfopen").addClass("iconfclosed");
    childRowsSpans.filter(".arrow").html('&#9658;');
    childRows.show(); //show all children
  }
  updateStripes();
}


function toggleInherit(id)
{
  var rows = $('tr.inherit.'+id);
  var img = $('tr.inherit_header.'+id+' img');
  var src = $(img).attr('src');
  if (rows.filter(':first').is(':visible')===true) {
    rows.css('display','none');
    $(img).attr('src',src.substring(0,src.length-8)+'closed.png');
  } else {
    rows.css('display','table-row'); // using show() causes jump in firefox
    $(img).attr('src',src.substring(0,src.length-10)+'open.png');
  }
}

var opened=true;
// in case HTML_COLORSTYLE is LIGHT or DARK the vars will be replaced, so we write them out explicitly and use double quotes
var plusImg  = [ "var(--fold-plus-image)",  "var(--fold-plus-image-relpath)" ];
var minusImg = [ "var(--fold-minus-image)", "var(--fold-minus-image-relpath)" ];

// toggle all folding blocks
function codefold_toggle_all(relPath) {
 if (opened) {
   $('#fold_all').css('background-image',plusImg[relPath]);
   $('div[id^=foldopen]').hide();
   $('div[id^=foldclosed]').show();
 } else {
   $('#fold_all').css('background-image',minusImg[relPath]);
   $('div[id^=foldopen]').show();
   $('div[id^=foldclosed]').hide();
 }
 opened=!opened;
}

// toggle single folding block
function codefold_toggle(id) {
  $('#foldopen'+id).toggle();
  $('#foldclosed'+id).toggle();
}
function init_codefold(relPath) {
  $('span[class=lineno]').css(
    {'padding-right':'4px',
     'margin-right':'2px',
     'display':'inline-block',
     'width':'54px',
     'background':'linear-gradient(var(--fold-line-color),var(--fold-line-color)) no-repeat 46px/2px 100%'
    });
  // add global toggle to first line
  $('span[class=lineno]:first').append('<span class="fold" id="fold_all" '+
                                             'onclick="javascript:codefold_toggle_all('+relPath+');" '+
                                             'style="background-image:'+minusImg[relPath]+';"></span>');
  // add vertical lines to other rows
  $('span[class=lineno]').not(':eq(0)').append('<span class="fold"></span>');
  // add toggle controls to lines with fold divs
  $('div[class=foldopen]').each(function() {
    // extract specific id to use
    var id    = $(this).attr('id').replace('foldopen','');
    // extract start and end foldable fragment attributes
    var start = $(this).attr('data-start');
    var end   = $(this).attr('data-end');
    // replace normal fold span with controls for the first line of a foldable fragment
    $(this).find('span[class=fold]:first').replaceWith('<span class="fold" '+
                                                       'onclick="javascript:codefold_toggle(\''+id+'\');" '+
                                                       'style="background-image:'+minusImg[relPath]+';"></span>');
    // append div for folded (closed) representation
    $(this).after('<div id="foldclosed'+id+'" class="foldclosed" style="display:none;"></div>');
    // extract the first line from the "open" section to represent closed content
    var line = $(this).children().first().clone();
    // remove any glow that might still be active on the original line
    $(line).removeClass('glow');
    if (start) {
      // if line already ends with a start marker (e.g. trailing {), remove it
      $(line).html($(line).html().replace(new RegExp('\\s*'+start+'\\s*$','g'),''));
    }
    // replace minus with plus symbol
    $(line).find('span[class=fold]').css('background-image',plusImg[relPath]);
    // append ellipsis
    $(line).append(' '+start+'<a href="javascript:codefold_toggle(\''+id+'\')">&#8230;</a>'+end);
    // insert constructed line into closed div
    $('#foldclosed'+id).html(line);
  });
}

/* @license-end */
