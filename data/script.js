if (!!window.EventSource) {
  var source = new EventSource('/events');

  source.onopen = function(e) {
    document.getElementById("server-status").innerHTML = "Online";
    document.getElementById("server-status").style.color = "green";
  };

  source.onerror = function(e) {
    if (e.target.readyState != EventSource.OPEN) {
      document.getElementById("server-status").innerHTML = "Offline";
      document.getElementById("server-status").style.color = "red";
    }
  };

  source.addEventListener('rotation', function(e) {
    document.getElementById('rotations').innerHTML = e.data;
  }, false);
}
