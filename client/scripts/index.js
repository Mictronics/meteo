document.addEventListener('DOMContentLoaded', () => {
  renderWindSpeedChart('#windspeedChart');

  renderHumidityChart('#humidityChart', 5, 30);

  setInterval(renderWindSpeedChart, 1000, '#windspeedChart');
  setInterval(renderHumidityChart, 1000, '#humidityChart', 5, 30);
});
