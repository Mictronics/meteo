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

/* Known server commands, same as in meteoserver.h. */
const ServerCmd = Object.freeze({
  Start: 0x1a,
  Stop: 0x2b,
  FromTo: 0x3c,
  Elevation: 0x4d,
  Heading: 0x5e,
  TimeSync: 0x6f
});

const SERVER_PORT = 10024;
/* Size of the packed t_packet_data in meteoserver.h. */
const PACKET_SIZE = 150;

/*
 * Data received from meteo websocket server.
 */
const serverData = Object.seal({
  recordStatus: 0,
  localTime: 0,
  maws_second: 0,
  maws_minute: 0,
  maws_hour: 0,
  topNumber: 0,
  flightNumber: 0,
  gpsStatus: 0,
  gpsMode: 0,
  gpsSatellitesVisible: 0,
  gpsSatellitesUsed: 0,
  gpsTime: 0,
  gpsHDOP: 0,
  gpsPDOP: 0,
  gpsLat: 0,
  gpsLon: 0,
  gpsAltMsl: 0,
  runwayElevation: 0,
  runwayHeading: 0,
  fromToStatus: 0,
  temperature: 0,
  humidity: 0,
  baroPressure: 0,
  windDirection: 0,
  windDirectionMean: 0,
  windspeed: 0,
  windspeedMean: 0,
  crossWindspeed: 0,
  crossWindspeedMean: 0,
  headWindspeed: 0,
  QFE: 0,
  QNH: 0,
  barometerHeight: 0
});

/*
 * Websocket for server communication.
 */
let socket = null;

const connection = navigator.connection || navigator.mozConnection || null;
if (connection === null) {
  console.info('Network Information API not supported.');
}

/*
 * Connects to the meteo server on the host that served this page.
 */
function connect() {
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new WebSocket(`${scheme}://${location.hostname}:${SERVER_PORT}`, 'broadcast');
  socket.binaryType = 'arraybuffer';

  socket.onmessage = (e) => {
    if (typeof e.data === 'string' || e.data.byteLength < PACKET_SIZE) {
      console.error('Unexpected message from server.');
      return;
    }

    const dv = new DataView(e.data);
    serverData.gpsHDOP = dv.getFloat64(0, true);
    serverData.gpsPDOP = dv.getFloat64(8, true);
    serverData.gpsLat = dv.getFloat64(16, true);
    serverData.gpsLon = dv.getFloat64(24, true);
    serverData.gpsAltMsl = dv.getFloat64(32, true);
    serverData.temperature = dv.getFloat64(40, true);
    serverData.baroPressure = dv.getFloat64(48, true);
    serverData.windspeed = dv.getFloat64(56, true);
    serverData.windspeedMean = dv.getFloat64(64, true);
    serverData.crossWindspeed = dv.getFloat64(72, true);
    serverData.crossWindspeedMean = dv.getFloat64(80, true);
    serverData.headWindspeed = dv.getFloat64(88, true);
    serverData.QFE = dv.getFloat64(96, true);
    serverData.QNH = dv.getFloat64(104, true);
    serverData.gpsTime = dv.getFloat64(112, true);
    serverData.localTime = dv.getFloat64(120, true);
    serverData.flightNumber = dv.getUint16(128, true);
    serverData.runwayHeading = dv.getUint16(130, true);
    serverData.runwayElevation = dv.getUint16(132, true);
    serverData.windDirection = dv.getUint16(134, true);
    serverData.windDirectionMean = dv.getUint16(136, true);
    serverData.barometerHeight = dv.getUint8(138);
    serverData.maws_hour = dv.getUint8(139);
    serverData.maws_minute = dv.getUint8(140);
    serverData.maws_second = dv.getUint8(141);
    serverData.humidity = dv.getUint8(142);
    serverData.topNumber = dv.getUint8(143);
    serverData.gpsStatus = dv.getUint8(144);
    serverData.gpsMode = dv.getUint8(145);
    serverData.gpsSatellitesVisible = dv.getUint8(146);
    serverData.gpsSatellitesUsed = dv.getUint8(147);
    serverData.recordStatus = dv.getUint8(148);
    serverData.fromToStatus = dv.getUint8(149);

    self.postMessage({ cmd: 'data', data: serverData });
  };

  socket.onerror = () => {
    console.error('Connection error.');
  };

  socket.onclose = () => {
    console.warn('Connection closed.');
    socket = null;
    self.postMessage({ cmd: 'disconnected', data: null });
    setTimeout(connect, 1000);
  };

  socket.onopen = () => {
    console.info(`Connected with ${location.hostname}:${SERVER_PORT}`);
    self.postMessage({ cmd: 'connected', data: null });
  };
}

/*
 * Get network connection info
 */
function getNetworkInfo() {
  if (connection !== null && connection.downlink !== undefined) {
    self.postMessage({
      cmd: 'network',
      data: connection.downlink /* In Mbps */
    });
  }
}

/*
 * Send a command to the server: command id, optionally followed by a 16 bit value
 * and, for Start, the 8 bit TOP number. Values are little endian.
 */
function sendCommand(id, value16, value8) {
  if (socket === null || socket.readyState !== WebSocket.OPEN) {
    return;
  }

  const buf = new ArrayBuffer(value8 === undefined ? 1 + (value16 === undefined ? 0 : 2) : 4);
  const dv = new DataView(buf);
  dv.setUint8(0, id);
  if (value16 !== undefined) {
    dv.setUint16(1, value16, true);
  }
  if (value8 !== undefined) {
    dv.setUint8(3, value8);
  }
  socket.send(buf);
}

self.onmessage = (e) => {
  const msg = e.data;
  switch (msg.cmd) {
    case 'connect':
      connect();
      break;
    case 'start':
      sendCommand(ServerCmd.Start, msg.data.flightNumber, msg.data.topNumber);
      break;
    case 'stop':
      sendCommand(ServerCmd.Stop);
      break;
    case 'fromto':
      sendCommand(ServerCmd.FromTo);
      break;
    case 'elevation':
      sendCommand(ServerCmd.Elevation, msg.data.elevation);
      break;
    case 'heading':
      sendCommand(ServerCmd.Heading, msg.data.heading);
      break;
    case 'timesync':
      sendCommand(ServerCmd.TimeSync);
      break;
    default:
      console.error(`Unknown command: ${msg.cmd}`);
  }
};

setInterval(getNetworkInfo, 5000);
