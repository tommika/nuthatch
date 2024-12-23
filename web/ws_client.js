// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
function ws_create() {
  var url;
  var loc = window.location;
  if(loc.protocol === "file:")  {
    url = "ws://localhost:8088";
  } else if(loc.protocol === "https:") {
    url = "wss://"+loc.hostname + ":" + loc.port;
  } else {
    url = "ws://"+loc.hostname + ":" + loc.port;
  }
  var i = loc.pathname.lastIndexOf("/")
  var path_prefix = loc.pathname.substring(0,i)
  url += path_prefix + "/ws";
  console.log("Connecting to url: "+url);
  return new WebSocket(url);
}

function ws_send_message(ws,str) {
  var sent = false;
  try {
    sent = ws.send(str);
    if(sent == undefined) {
      sent = true;
    }
  } catch(err) {
    sent = false;
    console.log("ws::send exception",err);
  }
return sent;
}
