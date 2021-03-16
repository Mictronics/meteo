// Part of WebMeteo, a Vaisalla weather data visualization.
//
// Copyright (c) 2021 Michael Wolf <michael@mictronics.de>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

let flightNumber = -1;
let topNumber = -1;
let recordStatus = -1;
let runwayHeading = -1;
let runwayElevation = -1;

// Create worker thread for server communication.
const serverCommunicationWorker = new Worker('./scripts/communication.worker.js');
// Init charts
const windspeedChart = new WindspeedChart('#windspeedChart', 0, 0);
const humidityChart = new HumidityChart('#humidityChart', 0, 0);

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
  // GPS time and date
  let d = new Date(0);
  d.setUTCSeconds(serverData.gpsTime);

  const timeGPS = `${d.getUTCHours().toString(10).padStart(2, '0')}:${d
    .getUTCMinutes()
    .toString(10)
    .padStart(2, '0')}:${d.getUTCSeconds().toString(10).padStart(2, '0')}`;

  // Get MAWS time
  d = new Date(
    d.getUTCFullYear(),
    d.getUTCMonth(),
    d.getUTCDay(),
    serverData.maws_hour,
    serverData.maws_minute,
    serverData.maws_second,
    0
  );
  const timeLocal = d.toLocaleTimeString();

  document.getElementById('localDate').innerHTML = d.toLocaleDateString();
  document.getElementById('localTime').innerHTML = `${timeLocal} / ${timeGPS}`;

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

  // Runway heading and elevation
  if (runwayElevation !== serverData.runwayElevation) {
    runwayElevation = serverData.runwayElevation;
    document.getElementById('elevationInput').value = runwayElevation.toFixed(0);
  }

  const hsiObject = document.getElementById('hsiObject').contentDocument.children[0];
  if (runwayHeading !== serverData.runwayHeading) {
    runwayHeading = serverData.runwayHeading;
    document.getElementById('runwayHeadingInput').value = runwayHeading.toFixed(0);
    hsiObject.getElementById(
      'aircraftSymbol'
    ).attributes.transform.value = `matrix(1,0,0,1,70,70) rotate(${serverData.runwayHeading},0,0) scale(1.5)`;
  }

  // GPS Information
  document.getElementById('gpsLat').innerHTML = `${serverData.gpsLat.toFixed(6)}°`;
  document.getElementById('gpsLon').innerHTML = `${serverData.gpsLon.toFixed(6)}°`;
  document.getElementById('gpsAltMsl').innerHTML = `${serverData.gpsAltMsl.toFixed(1)}`;
  document.getElementById(
    'gpsSatellites'
  ).innerHTML = `${serverData.gpsSatellitesUsed}/${serverData.gpsSatellitesVisible}`;
  document.getElementById('gpsStatus').innerHTML = `${gpsMode[serverData.gpsMode]} ${gpsStatus[serverData.gpsStatus]}`;
  document.getElementById('gpsDOP').innerHTML = `${serverData.gpsPDOP.toFixed(1)}/${serverData.gpsHDOP.toFixed(1)}`;

  // From-To button status
  const btn = document.getElementById('fromToButton');
  if (serverData.fromToStatus) {
    btn.classList.replace('btn-secondary', 'btn-warning');
  } else {
    btn.classList.replace('btn-warning', 'btn-secondary');
  }

  // Weather information
  document.getElementById('temperature').innerHTML = serverData.temperature.toFixed(1);
  document.getElementById('humidity').innerHTML = serverData.humidity.toFixed(0);
  document.getElementById('pressure').innerHTML = serverData.baroPressure.toFixed(1);
  document.getElementById('qnh').innerHTML = `${serverData.QNH.toFixed(1)}`;
  document.getElementById('qfe').innerHTML = `${serverData.QFE.toFixed(1)}`;
  document.getElementById('windDirectionMean').innerHTML = `${serverData.windDirectionMean.toFixed(0)}`;
  document.getElementById('windspeedMean').innerHTML = `${serverData.windspeedMean.toFixed(1)}`;
  document.getElementById('windDirection').innerHTML = `${serverData.windDirection.toFixed(0)}`;
  document.getElementById('windspeed').innerHTML = `${serverData.windspeed.toFixed(1)}`;

  if (serverData.crossWindspeed < 0.0) {
    document.getElementById('crossWindspeedLabel').innerHTML = 'Crosswind from left [kts]';
    document.getElementById('crossWindspeed').innerHTML = `${Math.abs(serverData.crossWindspeed).toFixed(1)}`;
  } else {
    document.getElementById('crossWindspeedLabel').innerHTML = 'Crosswind from right [kts]';
    document.getElementById('crossWindspeed').innerHTML = `${Math.abs(serverData.crossWindspeed).toFixed(1)}`;
  }

  if (serverData.headWindspeed < 0.0) {
    document.getElementById('headWindspeedLabel').innerHTML = 'Tailwind [kts]';
    document.getElementById('headWindspeed').innerHTML = `${Math.abs(serverData.headWindspeed).toFixed(1)}`;
  } else {
    document.getElementById('headWindspeedLabel').innerHTML = 'Headwind [kts]';
    document.getElementById('headWindspeed').innerHTML = `${Math.abs(serverData.headWindspeed).toFixed(1)}`;
  }

  hsiObject.getElementById(
    'windArrow'
  ).attributes.transform.value = `matrix(1,0,0,1,70,70) rotate(${serverData.windDirection},0,0)`;
  hsiObject.getElementById(
    'meanWindArrow'
  ).attributes.transform.value = `matrix(1,0,0,1,70,70) rotate(${serverData.windDirectionMean},0,0)`;

  // Update charts
  windspeedChart.Update(Math.abs(serverData.windspeedMean), Math.abs(serverData.crossWindspeed));
  humidityChart.Update(serverData.temperature, serverData.humidity);
}

// Start/stop recording
function OnRecordButtonClick(ev) {
  if (recordStatus === 0) {
    serverCommunicationWorker.postMessage({
      cmd: 'start',
      data: {
        flightNumber: Number.parseInt(document.getElementById('flightNumberInput').value, 10),
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
  serverCommunicationWorker.postMessage({
    cmd: 'fromto',
    data: null
  });
}

// Forward elevation changes to server
function OnElevationInputChange(ev) {
  const elevation = Number.parseInt(ev.target.value, 10);
  serverCommunicationWorker.postMessage({
    cmd: 'elevation',
    data: {
      elevation
    }
  });
}

// Forward runway heading changes to server
function OnRunwayHeadingInputChange(ev) {
  const heading = Number.parseInt(ev.target.value, 10);
  serverCommunicationWorker.postMessage({
    cmd: 'heading',
    data: {
      heading
    }
  });
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

  // Add event handlers
  document.getElementById('recordButton').addEventListener('click', OnRecordButtonClick);
  document.getElementById('fromToButton').addEventListener('click', OnFromToButtonClick);
  document.getElementById('elevationInput').addEventListener('change', OnElevationInputChange);
  document.getElementById('runwayHeadingInput').addEventListener('change', OnRunwayHeadingInputChange);

  // Finally, connect to weather station
  serverCommunicationWorker.postMessage({
    cmd: 'connect',
    data: null
  });
});
