// Part of WebMeteo, a Vaisalla weather data visualization.
//
// Copyright (c) 2026 Michael Wolf <michael@mictronics.de>
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

const MAX_ELEVATION_FT = 29030;

let flightNumber = -1;
let topNumber = -1;
let recordStatus = -1;
let mawsStatus = -1;
let runwayHeading = -1;
let runwayElevation = -1;
let hsiHeadingPending = false;

// Create worker thread for server communication.
const serverCommunicationWorker = new Worker('./scripts/communication.worker.js');
// Init charts
const windspeedChart = new WindspeedChart('#windspeedChart', 0, 0);
const humidityChart = new HumidityChart('#humidityChart', 0, 0);

const gpsStatus = [
  '', // Unknown: receiver sends no fix type (mode 1 shows No Fix)
  'SPS', // plain GPS (SPS Mode), without DGPS, PPS, RTK, DR, etc.
  'DGPS', // Differential GPS
  'RTK', // RTK fixed
  'RTK', // RTK float
  'DR', // Dead reckoning
  'GNSDR', // GNSS + dead reckoning
  'TIME', // Time only
  'SIM', // Simulation
  'PPS' // Precision Position Mode P(Y)
];

const gpsMode = [
  '', // 0 mode update not seen yet
  'No Fix', // 1 none
  '2D', // 2 good for latitude/longitude
  '3D' // 3 good for altitude/climb too
];

// Look up each DOM element only once.
const elementCache = {};
function $(id) {
  if (!elementCache[id]) {
    elementCache[id] = document.getElementById(id);
  }
  return elementCache[id];
}

function pad2(n) {
  return n.toString(10).padStart(2, '0');
}

// Root element of the HSI SVG, null as long as the SVG is not loaded.
function getHsi() {
  const doc = $('hsiObject').contentDocument;
  return doc ? doc.children[0] : null;
}

// Update local time indication periodically
function UpdateGui(serverData) {
  // GPS time, MAWS runs on UTC as well
  const hasGpsTime = serverData.gpsTime > 0;
  const gpsDate = new Date(serverData.gpsTime * 1000);
  const timeGPS = hasGpsTime
    ? `${pad2(gpsDate.getUTCHours())}:${pad2(gpsDate.getUTCMinutes())}:${pad2(gpsDate.getUTCSeconds())}`
    : '--:--:--';

  // MAWS time with the date taken from GPS
  const mawsDate = new Date(
    Date.UTC(
      hasGpsTime ? gpsDate.getUTCFullYear() : 1970,
      hasGpsTime ? gpsDate.getUTCMonth() : 0,
      hasGpsTime ? gpsDate.getUTCDate() : 1,
      serverData.maws_hour,
      serverData.maws_minute,
      serverData.maws_second
    )
  );
  const timeMaws = mawsDate.toLocaleTimeString(undefined, { timeZone: 'UTC' });

  $('localDate').textContent = hasGpsTime ? mawsDate.toLocaleDateString(undefined, { timeZone: 'UTC' }) : '--.--.----';
  $('localTime').textContent = `${timeMaws} / ${timeGPS}`;

  // Flight and top number
  const fn = $('flightNumberInput');
  const tn = $('topNumberInput');
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

  // MAWS watchdog: serial data stopped arriving
  if (mawsStatus !== serverData.mawsStatus) {
    if (mawsStatus !== -1 || serverData.mawsStatus === 0) {
      showNoty(serverData.mawsStatus ? 'MAWS data resumed' : 'No data from MAWS!', serverData.mawsStatus ? 'success' : 'error');
    }
    mawsStatus = serverData.mawsStatus;
    $('mawsLabel').textContent = mawsStatus ? 'MAWS / GPS UTC' : 'MAWS NO DATA / GPS UTC';
    $('mawsLabel').classList.toggle('text-danger', !mawsStatus);
    $('mawsLabel').classList.toggle('text-black-50', !!mawsStatus);
  }

  // Record status: 0 idle, 1 recording, 2 failed (disk error)
  const recording = serverData.recordStatus === 1;
  if (serverData.recordStatus === 2 && recordStatus !== 2) {
    showNoty('Recording failed! Check storage, no data is being saved.', 'error');
  }
  recordStatus = serverData.recordStatus;
  fn.disabled = recording;
  tn.disabled = recording;
  $('recordSpinner').classList.toggle('d-none', !recording);
  $('recordLabel').textContent = ['Record', 'Stop', 'FAILED - retry'][recordStatus] || 'Record';
  $('recordButton').classList.toggle('btn-danger', recordStatus !== 2);
  $('recordButton').classList.toggle('btn-warning', recordStatus === 2);

  // Runway heading and elevation
  if (runwayElevation !== serverData.runwayElevation) {
    runwayElevation = serverData.runwayElevation;
    $('elevationInput').value = runwayElevation.toFixed(0);
  }

  if (runwayHeading !== serverData.runwayHeading) {
    runwayHeading = serverData.runwayHeading;
    $('runwayHeadingInput').value = runwayHeading.toFixed(0);
    hsiHeadingPending = true;
  }

  // The SVG may still be loading when the first data arrives
  const hsi = getHsi();
  if (hsi !== null) {
    if (hsiHeadingPending) {
      hsi
        .getElementById('aircraftSymbol')
        .setAttribute('transform', `matrix(1,0,0,1,70,70) rotate(${serverData.runwayHeading},0,0) scale(1.5)`);
      hsiHeadingPending = false;
    }
    hsi
      .getElementById('windArrow')
      .setAttribute('transform', `matrix(1,0,0,1,70,70) rotate(${serverData.windDirection},0,0)`);
    hsi
      .getElementById('meanWindArrow')
      .setAttribute('transform', `matrix(1,0,0,1,70,70) rotate(${serverData.windDirectionMean},0,0)`);
  }

  // GPS Information
  $('gpsLat').textContent = `${serverData.gpsLat.toFixed(6)}°`;
  $('gpsLon').textContent = `${serverData.gpsLon.toFixed(6)}°`;
  $('gpsAltMsl').textContent = `${serverData.gpsAltMsl.toFixed(1)}`;
  $('gpsSatellites').textContent = `${serverData.gpsSatellitesUsed}/${serverData.gpsSatellitesVisible}`;
  $('gpsStatus').textContent = `${gpsMode[serverData.gpsMode] || ''} ${gpsStatus[serverData.gpsStatus] || ''}`;
  $('gpsDOP').textContent = `${serverData.gpsPDOP.toFixed(1)}/${serverData.gpsHDOP.toFixed(1)}`;

  // Time sync button only enabled when GPS time is valid
  let btn = $('timeSyncButton');
  if (serverData.gpsMode > 1) {
    btn.classList.replace('btn-outline-light', 'btn-info');
    btn.classList.remove('disabled');
  } else {
    btn.classList.replace('btn-info', 'btn-outline-light');
    btn.classList.add('disabled');
  }

  // From-To button status
  btn = $('fromToButton');
  if (serverData.fromToStatus) {
    btn.classList.replace('btn-secondary', 'btn-warning');
  } else {
    btn.classList.replace('btn-warning', 'btn-secondary');
  }

  // Weather information
  $('temperature').textContent = serverData.temperature.toFixed(1);
  $('humidity').textContent = serverData.humidity.toFixed(0);
  $('pressure').textContent = serverData.baroPressure.toFixed(1);
  $('qnh').textContent = serverData.QNH.toFixed(1);
  $('qfe').textContent = serverData.QFE.toFixed(1);
  $('windDirectionMean').textContent = serverData.windDirectionMean.toFixed(0);
  $('windspeedMean').textContent = serverData.windspeedMean.toFixed(1);
  $('windDirection').textContent = serverData.windDirection.toFixed(0);
  $('windspeed').textContent = serverData.windspeed.toFixed(1);

  $('crossWindspeedLabel').textContent =
    serverData.crossWindspeed < 0.0 ? 'Crosswind from left [kts]' : 'Crosswind from right [kts]';
  $('crossWindspeed').textContent = Math.abs(serverData.crossWindspeed).toFixed(1);
  $('headWindspeedLabel').textContent = serverData.headWindspeed < 0.0 ? 'Tailwind [kts]' : 'Headwind [kts]';
  $('headWindspeed').textContent = Math.abs(serverData.headWindspeed).toFixed(1);

  // Update charts
  windspeedChart.Update(Math.abs(serverData.windspeedMean), Math.abs(serverData.crossWindspeedMean));
  humidityChart.Update(serverData.temperature, serverData.humidity);
}

// Start/stop recording
function OnRecordButtonClick(ev) {
  if (recordStatus !== 1) {
    const top = Number.parseInt($('topNumberInput').value, 10);
    serverCommunicationWorker.postMessage({
      cmd: 'start',
      data: {
        flightNumber: Number.parseInt($('flightNumberInput').value, 10),
        topNumber: Number.isNaN(top) ? topNumber : top
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

// Forward elevation changes to server, restore the last server value if the input is invalid
function OnElevationInputChange(ev) {
  const input = ev.target;
  const elevation = Number.parseInt(input.value, 10);
  if (!(elevation >= 0 && elevation <= MAX_ELEVATION_FT)) {
    input.value = runwayElevation >= 0 ? runwayElevation : '';
    return;
  }
  serverCommunicationWorker.postMessage({
    cmd: 'elevation',
    data: {
      elevation
    }
  });
}

// Forward runway heading changes to server, restore the last server value if the input is invalid
function OnRunwayHeadingInputChange(ev) {
  const input = ev.target;
  const heading = Number.parseInt(input.value, 10);
  if (!(heading >= 0 && heading <= 359)) {
    input.value = runwayHeading >= 0 ? runwayHeading : '';
    return;
  }
  serverCommunicationWorker.postMessage({
    cmd: 'heading',
    data: {
      heading
    }
  });
}

// Start time sync from GPS to MAWS
function OnTimeSyncButtonClick(ev) {
  serverCommunicationWorker.postMessage({
    cmd: 'timesync',
    data: null
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
  // version.js is generated by make, absent in a plain checkout
  $('appVersion').textContent = typeof APP_VERSION === 'undefined' ? '' : `v${APP_VERSION}`;

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
        $('linkSpeed').textContent = `${msg.data}Mbps`;
        break;
      case 'data':
        UpdateGui(msg.data);
        break;
      default:
        console.error(`Unknown command: ${msg.cmd}`);
    }
  };

  // Add event handlers
  $('recordButton').addEventListener('click', OnRecordButtonClick);
  $('fromToButton').addEventListener('click', OnFromToButtonClick);
  $('timeSyncButton').addEventListener('click', OnTimeSyncButtonClick);
  $('elevationInput').addEventListener('change', OnElevationInputChange);
  $('runwayHeadingInput').addEventListener('change', OnRunwayHeadingInputChange);

  // Finally, connect to weather station
  serverCommunicationWorker.postMessage({
    cmd: 'connect',
    data: null
  });
});
