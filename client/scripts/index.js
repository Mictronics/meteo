let isFromRunwayDirection = false;
let flightNumber = -1;
let topNumber = -1;
let recordStatus = -1;

// Create worker thread for server communication.
const serverCommunicationWorker = new Worker(
  './scripts/communication.worker.js'
);

const gpsStatus = [
  'No Fix', // No Fix
  'SPS', // plain GPS (SPS Mode), without DGPS, PPS, RTK, DR, etc.
  'DGPS', // Differential GPS
  'RTK', // RTK float
  'RTK', // RTK fixed
  'DR', // Dead reckoning
  'GNSDR', // GNSS + dead reckoning
  'TIME', // Time only
  'SIM', // Simulation
  'PPS' // Precision Position Mode P(Y)
];

const gpsMode = [
  '', // 0 mode update not seen yet
  '', // 1 none
  '2D', // 2 good for latitude/longitude
  '3D' // 3 good for altitude/climb too
];

// Update local time indication periodically
function UpdateGui(serverData) {
  // Server local time and date
  let d = new Date(
    1900 + serverData.year,
    serverData.month,
    serverData.day,
    serverData.hour,
    serverData.minute,
    serverData.second,
    0
  );

  const tm_local = d.toLocaleTimeString();
  d = new Date(0);
  d.setUTCSeconds(serverData.gpsTime);

  const tm_gps =
    d.getUTCHours().toString(10).padStart(2, '0') +
    ':' +
    d.getUTCMinutes().toString(10).padStart(2, '0') +
    ':' +
    d.getUTCSeconds().toString(10).padStart(2, '0');

  document.getElementById('localDate').innerHTML = d.toLocaleDateString();
  document.getElementById('localTime').innerHTML = `${tm_local} / ${tm_gps}`;

  document.getElementById(
    'timeDifference'
  ).innerHTML = `${serverData.timeDiff.toPrecision(4)} sec`;

  // Flight and top number
  const fn = document.getElementById('flightNumberInput');
  const tn = document.getElementById('topNumberInput');
  if (flightNumber < 0) {
    fn.value = serverData.flightNumber;
    flightNumber = serverData.flightNumber;
  }

  if (fn.value < serverData.flightNumber) {
    fn.classList.add('is-invalid');
  } else {
    fn.classList.remove('is-invalid');
  }

  if (serverData.topNumber > topNumber) {
    tn.value = serverData.topNumber;
    topNumber = serverData.topNumber;
  }

  if (tn.value < serverData.topNumber) {
    tn.classList.add('is-invalid');
  } else {
    tn.classList.remove('is-invalid');
  }

  recordStatus = serverData.recordStatus;
  if (recordStatus === 0) {
    fn.disabled = false;
    tn.disabled = false;
    document.getElementById('recordSpinner').classList.add('d-none');
  } else {
    fn.disabled = true;
    tn.disabled = true;
    document.getElementById('recordSpinner').classList.remove('d-none');
  }

  // GPS Information
  document.getElementById(
    'gpsLat'
  ).innerHTML = `${serverData.gpsLat.toPrecision(8)}°`;
  document.getElementById(
    'gpsLon'
  ).innerHTML = `${serverData.gpsLon.toPrecision(8)}°`;
  document.getElementById(
    'gpsAltMsl'
  ).innerHTML = `${serverData.gpsAltMsl.toPrecision(5)}`;
  document.getElementById(
    'gpsSatellites'
  ).innerHTML = `${serverData.gpsSatellitesUsed}/${serverData.gpsSatellitesVisible}`;
  document.getElementById('gpsStatus').innerHTML = `${
    gpsMode[serverData.gpsMode]
  } ${gpsStatus[serverData.gpsStatus]}`;
  document.getElementById(
    'gpsDOP'
  ).innerHTML = `${serverData.gpsPDOP.toPrecision(
    2
  )}/${serverData.gpsHDOP.toPrecision(2)}`;
}

// Show settings dialog
function OnSettingsButtonClick(ev) {}

// Start/stop recording
function OnRecordButtonClick(ev) {
  if (recordStatus === 0) {
    serverCommunicationWorker.postMessage({
      cmd: 'start',
      data: {
        flightNumber: Number.parseInt(
          document.getElementById('flightNumberInput').value,
          10
        ),
        topNumber
      }
    });
  } else {
    serverCommunicationWorker.postMessage({
      cmd: 'stop',
      data: null
    });
  }
}

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

  // Finally, connect to weather station
  serverCommunicationWorker.postMessage({
    cmd: 'connect',
    data: null
  });
});
