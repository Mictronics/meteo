// Update local time indication periodically
function UpdateLocalTime() {
  const d = new Date();
  document.getElementById('localDate').innerHTML = d.toLocaleDateString();
  document.getElementById('localTime').innerHTML = d.toLocaleTimeString();
}

// Start application as soon as DOM is fully loaded
document.addEventListener('DOMContentLoaded', () => {
  // Init charts
  const windspeedChart = new WindspeedChart('#windspeedChart', 0);
  const humidityChart = new HumidityChart('#humidityChart', 0, 0);
  UpdateLocalTime();
  // Init local time output
  setInterval(UpdateLocalTime, 1000);
  // FIXME Remove this, demo only
  setInterval(windspeedChart.Update.bind(windspeedChart), 1000, 8);
  setInterval(humidityChart.Update.bind(humidityChart), 1000, 20, 50);
});
