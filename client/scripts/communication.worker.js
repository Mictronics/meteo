/* Know server commands, same as in meteoserver.h. */
const ServerCmd = Object.freeze({
  Start: 0x1a,
  Stop: 0x2b,
  FromTo: 0x3c,
  Elevation: 0x4d,
  Heading: 0x5e
});

/*
 * Data received from meteo websocket server.
 */
const serverData = Object.seal({
  recordStatus: 0,
  maws_second: 0,
  maws_minute: 0,
  maws_hour: 0,
  day: 0,
  month: 0,
  year: 0,
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
  headWindspeed: 0,
  QFE: 0,
  QNH: 0,
  barometerHeight: 0
});

/*
 * Websocket for server communication.
 */
let socket8080 = null;

const connection = navigator.connection || navigator.mozConnection || null;
if (connection === null) {
  console.error('Network Information API not supported.');
}

/**
 * Because JavaScript does not currently include standard support for 64-bit integer values,
 * DataView does not offer native 64-bit operations.
 */
function getUint64(dataview, byteOffset, littleEndian) {
  // split 64-bit number into two 32-bit (4-byte) parts
  const left = dataview.getUint32(byteOffset, littleEndian);
  const right = dataview.getUint32(byteOffset + 4, littleEndian);

  // combine the two 32-bit values
  const combined = littleEndian ? left + 2 ** 32 * right : 2 ** 32 * left + right;

  if (!Number.isSafeInteger(combined)) console.warn(combined, 'exceeds MAX_SAFE_INTEGER. Precision may be lost');

  return combined;
}

/*
 * Connects with port 8080
 */
function connect8080() {
  console.info(`Location hostname: ${location.hostname}`);

  socket8080 = new WebSocket(`ws://${location.hostname}:10024`, 'broadcast');
  socket8080.binaryType = 'arraybuffer';

  socket8080.onmessage = (e) => {
    if (typeof e.data === 'string') {
      //  string is only if CRC is NOK
      console.error(`Wrong type of received message: ${e.data}`);
    } else {
      const arr = new Uint8Array(e.data);
      const dv = new DataView(arr.buffer, 0, arr.length);
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
      serverData.headWindspeed = dv.getFloat64(80, true);
      serverData.QFE = dv.getFloat64(88, true);
      serverData.QNH = dv.getFloat64(96, true);
      serverData.gpsTime = dv.getFloat64(104, true);
      serverData.localTime = dv.getFloat64(112, true);
      serverData.flightNumber = dv.getInt16(120, true);
      serverData.runwayHeading = dv.getInt16(122, true);
      serverData.runwayElevation = dv.getInt16(124, true);
      serverData.windDirection = dv.getInt16(126, true);
      serverData.windDirectionMean = dv.getInt16(128, true);
      serverData.barometerHeight = dv.getInt8(130, true);
      serverData.maws_hour = dv.getInt8(131, true);
      serverData.maws_minute = dv.getInt8(132, true);
      serverData.maws_second = dv.getInt8(133, true);
      serverData.humidity = dv.getInt8(134, true);
      serverData.topNumber = dv.getInt8(135, true);
      serverData.gpsStatus = dv.getInt8(136, true);
      serverData.gpsMode = dv.getInt8(137, true);
      serverData.gpsSatellitesVisible = dv.getInt8(138, true);
      serverData.gpsSatellitesUsed = dv.getInt8(139, true);
      serverData.recordStatus = dv.getInt8(140, true);
      serverData.fromToStatus = dv.getInt8(141, true);

      self.postMessage({ cmd: 'data', data: serverData });
    }
  };

  socket8080.onclose = () => {
    console.warn('Connection closed.');
    socket8080 = null;
    self.postMessage({ cmd: 'disconnected', data: null });
    setTimeout(() => {
      connect8080();
    }, 1000);
  };

  /*
   * Event functions for socket
   */
  socket8080.onopen = () => {
    console.info('Connected with port 8080');
    self.postMessage({ cmd: 'connected', data: null });
  };
}

/*
 * Get network connection info
 */
function getNetworkInfo() {
  if (connection !== null) {
    const { type } = connection;
    if (type !== undefined) {
      /* type not available in all browsers? */
    }
    self.postMessage({
      cmd: 'network',
      data: connection.downlink
    }); /* In Mbps */
  }
}

self.onmessage = (e) => {
  const msg = e.data;
  switch (msg.cmd) {
    case 'connect':
      connect8080();
      break;
    case 'start':
      if (socket8080 !== null && socket8080.readyState === 1) {
        const buf = new ArrayBuffer(4);
        const dv = new DataView(buf);
        dv.setUint8(0, ServerCmd.Start);
        dv.setUint16(1, msg.data.flightNumber, true);
        dv.setUint8(3, msg.data.topNumber);
        socket8080.send(buf);
      }
      break;
    case 'stop':
      if (socket8080 !== null && socket8080.readyState === 1) {
        const buf = new ArrayBuffer(1);
        const dv = new DataView(buf);
        dv.setUint8(0, ServerCmd.Stop);
        socket8080.send(buf);
      }
      break;
    case 'fromto':
      if (socket8080 !== null && socket8080.readyState === 1) {
        const buf = new ArrayBuffer(1);
        const dv = new DataView(buf);
        dv.setUint8(0, ServerCmd.FromTo);
        socket8080.send(buf);
      }
      break;
    case 'elevation':
      if (socket8080 !== null && socket8080.readyState === 1) {
        const buf = new ArrayBuffer(3);
        const dv = new DataView(buf);
        dv.setUint8(0, ServerCmd.Elevation);
        dv.setUint16(1, msg.data.elevation, true);
        socket8080.send(buf);
      }
      break;
    case 'heading':
      if (socket8080 !== null && socket8080.readyState === 1) {
        const buf = new ArrayBuffer(3);
        const dv = new DataView(buf);
        dv.setUint8(0, ServerCmd.Heading);
        dv.setUint16(1, msg.data.heading, true);
        socket8080.send(buf);
      }
      break;
    default:
      console.error(`Unknown command: ${msg.cmd}`);
  }
};

setInterval(() => {
  getNetworkInfo();
}, 5000);
