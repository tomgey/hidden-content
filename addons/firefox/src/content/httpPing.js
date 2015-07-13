function httpPing(address, onsuccess, onfail, timeout = 3000)
{
  var ping_req = new XMLHttpRequest();
  ping_req.timeout = timeout;
  ping_req.ontimeout = function() { onfail(address); };
  ping_req.onloadend = function()
  {
    if( ping_req.status == 200 )
      onsuccess(address);
    else
      onfail(address);
  }

  ping_req.open('GET', address, true);
  ping_req.send(null);
}
