window.onload = function() {
  fetch("/initial_rotations")
    .then(response => response.text())
    .then(data => {
      document.getElementById("rotations").innerHTML = data;
    });
  
  // Initialize speed display
  updateSpeedDisplay(0, 0, 0);
  updateRotationState(false, 0);
};

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

  source.addEventListener('speed', function(e) {
    const speeds = e.data.split(',');
    updateSpeedDisplay(parseFloat(speeds[0]), parseFloat(speeds[1]), parseFloat(speeds[2]));
  }, false);

  source.addEventListener('state', function(e) {
    const states = e.data.split(',');
    updateRotationState(states[0] === '1', parseInt(states[1]));
  }, false);

  var debug_source = new EventSource('/debug');
  debug_source.addEventListener('debug', function(e) {
    console.log(e.data);
  }, false);
}

function updateSpeedDisplay(current, max, avg) {
  document.getElementById('current-speed').innerHTML = current.toFixed(1);
  document.getElementById('max-speed').innerHTML = max.toFixed(1);
  document.getElementById('avg-speed').innerHTML = avg.toFixed(1);
}

function updateRotationState(isRotating, timeSinceMovement) {
  const statusElement = document.getElementById('rotation-status');
  const timeElement = document.getElementById('time-since-movement');
  const stoppedStatusElement = document.getElementById('stopped-status');
  
  if (isRotating) {
    statusElement.innerHTML = 'ROTATING';
    statusElement.style.color = '#4CAF50';
    stoppedStatusElement.style.display = 'none';
  } else {
    statusElement.innerHTML = 'STOPPED';
    statusElement.style.color = '#f44336';
    // Show stopped status when stopped for more than 2 seconds
    if (timeSinceMovement > 2000) {
      stoppedStatusElement.style.display = 'flex';
    } else {
      stoppedStatusElement.style.display = 'none';
    }
  }
  
  timeElement.innerHTML = (timeSinceMovement / 1000).toFixed(1) + 's';
}
