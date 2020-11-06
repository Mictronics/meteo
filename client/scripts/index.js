let isFromRunwayDirection = false;
let isMasterControl = false;

// Update local time indication periodically
function UpdateGui(serverData) {
  const d = new Date(
    1900 + serverData.tm_year,
    serverData.tm_mon,
    serverData.tm_mday,
    serverData.tm_hour,
    serverData.tm_min,
    serverData.tm_sec,
    0
  );
  document.getElementById('localDate').innerHTML = d.toLocaleDateString();
  document.getElementById('localTime').innerHTML = d.toLocaleTimeString();
}

// Show settings dialog
function OnSettingsButtonClick(ev) {}

// Start/stop recording
function OnRecordButtonClick(ev) {}

// Change runway direction by 180°
function OnFromToButtonClick(ev) {
  const btn = document.getElementById('fromToButton');
  if (isFromRunwayDirection) {
    btn.classList.replace('btn-warning', 'btn-secondary');
    isFromRunwayDirection = false;
  } else {
    btn.classList.replace('btn-secondary', 'btn-warning');
    isFromRunwayDirection = true;
  }
}

// Change client status of master control
function OnMasterControlButtonClick(ev) {}

// Show new noty of specific type
function showNoty(text, type) {
  new Noty({
    layout: 'topLeft',
    progressBar: false,
    text,
    theme: 'bootstrap-v4',
    timeout: 1500,
    type,
    container: '#noty'
  }).show();
}

// Start application as soon as DOM is fully loaded
document.addEventListener('DOMContentLoaded', () => {
  // Init charts
  const windspeedChart = new WindspeedChart('#windspeedChart', 0);
  const humidityChart = new HumidityChart('#humidityChart', 0, 0);

  // Create worker thread for server communication.
  const serverCommunicationWorker = new Worker(
    './scripts/communication.worker.js'
  );

  serverCommunicationWorker.onmessage = (e) => {
    const msg = e.data;
    switch (msg.cmd) {
      case 'connected':
        showNoty('Weather station connected', 'success');
        break;
      case 'disconnected':
        showNoty('Weather station disconnected', 'error');
        break;
      case 'network':
        document.getElementById('linkSpeed').innerHTML = `${msg.data}Mbps`;
        break;
      case 'data':
        UpdateGui(msg.data);
        break;
      default:
        console.error(`Unknown command: ${msg.cmd}`);
    }
  };

  // Add button click events
  document
    .getElementById('settingsButton')
    .addEventListener('click', OnSettingsButtonClick);
  document
    .getElementById('recordButton')
    .addEventListener('click', OnRecordButtonClick);
  document
    .getElementById('fromToButton')
    .addEventListener('click', OnFromToButtonClick);
  document
    .getElementById('masterControlButton')
    .addEventListener('click', OnMasterControlButtonClick);

  // Finally, connect to weather station
  serverCommunicationWorker.postMessage({
    cmd: 'connect',
    data: null
  });
});
