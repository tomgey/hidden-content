/**
 *
 */
function appendBBsFromRange(bbs, range, offset, region_cfg)
{
  let scale = 1.0; // TODO get real scale somehow
  let rects = [range.getBoundingClientRect()];//range.getClientRects();
  for(let i = 0; i < rects.length; ++i)
  {
    let bb = rects[i];
    if( bb.width > 2 && bb.height > 2 )
    {
      let l = Math.round(offset[0] + scale * (bb.left - 1));
      let r = Math.round(offset[0] + scale * (bb.right + 1));
      let t = Math.round(offset[1] + scale * (bb.top - 1));
      let b = Math.round(offset[1] + scale * (bb.bottom));
      bbs.push([[l, t], [r, t], [r, b], [l, b], region_cfg]);
    }
  }
}

/**
 *
 */
function searchDocument(id)
{
  let bbs = new Array();
  let view = document.defaultView;

  let id_regex = id.replace(/[\s-._]+/g, "[\\s-._]+");
  let textnodes = document.evaluate("//body//*/text()", document, null,  XPathResult.ANY_TYPE, null);

  let node = null;
  while(node = textnodes.iterateNext())
  {
    let node_string =  node.nodeValue;
    let regex = new RegExp(id_regex, "ig");
    let m;
    while( (m = regex.exec(node_string)) !== null )
    {
      let range = document.createRange();
      range.setStart(node, m.index);
      range.setEnd(node, m.index + m[0].length);
      appendBBsFromRange(bbs, range, [view.scrollX, view.scrollY], {ref: 'scroll'});
    }
  }

  // find area bounding boxes
//  var areaBBS = searchAreaTitles(doc, id);
//  bbs = bbs.concat(areaBBS);

  return bbs;
}
