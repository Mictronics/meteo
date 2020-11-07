/*
 * Data received from meteo websocket server.
 */
const serverData = Object.seal({
  master_client: 0,
  record_status: 0,
  tm_sec: 0,
  tm_min: 0,
  tm_hour: 0,
  tm_mday: 0,
  tm_mon: 0,
  tm_year: 0,
  top_number: 0,
  flight_number: 0,
  gps_status: 0,
  gps_satellites_visible: 0,
  gps_satellites_used: 0,
  gps_time: 0,
  gps_hdop: 0,
  gps_pdop: 0,
  gps_lat: 0,
  gps_lon: 0,
  gps_alt_msl: 0,
  runway_elevation: 0,
  runway_heading: 0,
  temperature: 0,
  humidity: 0,
  baro_pressure: 0,
  wind_direction: 0,
  windspeed: 0,
  cross_windspeed: 0,
  head_windspeed: 0,
  baro_qfe: 0,
  baro_qnh: 0,
  barometer_height: 0
});

/*
 * Websocket for server communication.
 */
let socket8080 = null;

const connection = navigator.connection || navigator.mozConnection || null;
if (connection === null) {
  console.error('Network Information API not supported.');
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
      let dv = new DataView(arr.buffer, 0, 139);
      serverData.gps_hdop = dv.getFloat64(0, true);
      serverData.gps_pdop = dv.getFloat64(8, true);
      serverData.gps_lat = dv.getFloat64(16, true);
      serverData.gps_lon = dv.getFloat64(24, true);
      serverData.gps_alt_msl = dv.getFloat64(32, true);
      serverData.barometer_height = dv.getFloat64(40, true);
      serverData.runway_elevation = dv.getFloat64(48, true);
      serverData.temperature = dv.getFloat64(56, true);
      serverData.baro_pressure = dv.getFloat64(64, true);
      serverData.windspeed = dv.getFloat64(72, true);
      serverData.cross_windspeed = dv.getFloat64(80, true);
      serverData.head_windspeed = dv.getFloat64(88, true);
      serverData.baro_qfe = dv.getFloat64(96, true);
      serverData.baro_qnh = dv.getFloat64(104, true);
      const t_msb = dv.getUint32(112, true);
      const t_lsb = dv.getUint32(116, true);
      serverData.gps_time = 0;
      serverData.flight_number = dv.getInt16(120, true);
      serverData.runway_heading = dv.getInt16(122, true);
      serverData.wind_direction = dv.getInt16(124, true);
      serverData.tm_year = dv.getInt16(126, true);
      serverData.tm_mon = dv.getInt8(128, true);
      serverData.tm_mday = dv.getInt8(129, true);
      serverData.tm_hour = dv.getInt8(130, true);
      serverData.tm_min = dv.getInt8(131, true);
      serverData.tm_sec = dv.getInt8(132, true);
      serverData.humidity = dv.getInt8(133, true);
      serverData.top_number = dv.getInt8(134, true);
      serverData.gps_status = dv.getInt8(135, true);
      serverData.gps_satellites_visible = dv.getInt8(136, true);
      serverData.gps_satellites_used = dv.getInt8(137, true);
      serverData.record_status = dv.getInt8(138, true);

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
    default:
      console.error(`Unknown command: ${msg.cmd}`);
  }
};

setInterval(() => {
  getNetworkInfo();
}, 5000);
