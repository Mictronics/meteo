class WindspeedChart {
  constructor(element, windspeed) {
    // DOM element id where chart is plotted in
    this.element = element;
    // Add first data pair
    this.windSpeedData = [
      {
        date: new Date(),
        speed: windspeed
      }
    ];
    // Accessor that creates min and max values from date in data
    this.xExtent = fc.extentDate().accessors([(d) => d.date]);
    // Windspeed as line series
    this.windSpeed = fc
      .seriesSvgLine()
      .mainValue((d) => d.speed)
      .crossValue((d) => d.date);
    // Grid lines
    this.windSpeedGridLines = fc
      .annotationSvgGridline()
      .yTicks(15)
      .xTicks(12)
      .xDecorate((sel) => sel.enter().classed('xWindSpeedGridLines', true))
      .yDecorate((sel) => sel.enter().classed('yWindSpeedGridLines', true));
    // Cross wind limit line
    this.crossWindLimit = fc
      .annotationSvgLine()
      .orient('horizontal')
      .value(5)
      .decorate((sel) =>
        sel
          .enter()
          .attr(
            'style',
            'stroke: blue !important; stroke-width: 2px; stroke-dasharray: 5,5;'
          )
      );
    // Total wind limit line
    this.totalWindLimit = fc
      .annotationSvgLine()
      .orient('horizontal')
      .value(10)
      .decorate((sel) =>
        sel
          .enter()
          .attr(
            'style',
            'stroke: red !important; stroke-width: 2px; stroke-dasharray: 5,5;'
          )
      );
    // Combine all chart elements in multi series
    this.multiWindSpeedSeries = fc
      .seriesSvgMulti()
      .series([
        this.windSpeedGridLines,
        this.windSpeed,
        this.crossWindLimit,
        this.totalWindLimit
      ]);
    // Define windspeed chart
    this.windSpeedChart = fc
      .chartCartesian(d3.scaleTime(), d3.scaleLinear())
      .yOrient('right')
      .yDomain([0, 15])
      .xDomain(this.xExtent(this.windSpeedData))
      .svgPlotArea(this.multiWindSpeedSeries)
      .xLabel('Time [s]')
      .yLabel('Windspeed [kts]')
      .xTickFormat(d3.timeFormat('%S'))
      .yTicks(15);
    // Draw windspeed chart first time
    this.windSpeedChart.xDomain(this.xExtent(this.windSpeedData));
    d3.select(this.element).datum(this.windSpeedData).call(this.windSpeedChart);
  }

  // Update windspeed chart with new value
  Update(windspeed) {
    this.windSpeedData.push({
      date: new Date(),
      speed: windspeed
    });
    // Keep only last 60s of windspeed data
    if (this.windSpeedData.length > 60) {
      this.windSpeedData.shift();
    }

    this.windSpeedChart.xDomain(this.xExtent(this.windSpeedData));
    d3.select(this.element).datum(this.windSpeedData).call(this.windSpeedChart);
  }
}

class HumidityChart {
  constructor(element, temperature, humidity) {
    // DOM element id where chart is plotted in
    this.element = element;
    // ICAO window definitions
    this.window0dB = [
      [2, 95],
      [30, 95],
      [30, 35],
      [15, 50],
      [2, 90],
      [2, 95]
    ];

    this.window10dB = [
      [2, 77.04384],
      [3, 73.36282],
      [4, 69.88155],
      [5, 66.59008],
      [6, 63.47846],
      [7, 60.54016],
      [8, 57.77197],
      [9, 55.16882],
      [10, 52.72404],
      [11, 50.42991],
      [12, 48.27807],
      [13, 46.25986],
      [14, 44.36656],
      [15, 42.58696],
      [16, 40.90427],
      [17, 39.30388],
      [18, 37.77425],
      [19, 36.30629],
      [20, 34.89287],
      [21, 33.52839],
      [22, 32.21073],
      [23, 30.9483],
      [24, 29.74937],
      [25, 28.61871],
      [26, 27.55843],
      [27, 26.56598],
      [28, 25.63014],
      [29, 24.74022],
      [30, 23.88776],
      [31, 23.0661],
      [32, 22.27008],
      [33, 21.49617],
      [34, 20.743],
      [35, 20.00973],
      [35, 20],
      [35, 95],
      [2, 95],
      [2, 77.04384]
    ];

    this.window12dB = [
      [-4, 82.617969],
      [-3, 78.47249],
      [-2, 74.48067],
      [-1, 70.68754],
      [0, 67.11999],
      [1, 63.79138],
      [2, 60.7052],
      [3, 57.857849],
      [4, 55.24078],
      [5, 52.83713],
      [6, 50.61007],
      [7, 48.52552],
      [8, 46.55715],
      [9, 44.68477],
      [10, 42.89301],
      [11, 41.17283],
      [12, 39.52266],
      [13, 37.94129],
      [14, 36.42703],
      [15, 34.97783],
      [16, 33.59148],
      [17, 32.26748],
      [18, 31.00678],
      [19, 29.80912],
      [20, 28.6733],
      [21, 27.59735],
      [22, 26.5788],
      [23, 25.61485],
      [24, 24.70247],
      [25, 23.83691],
      [26, 23.01024],
      [27, 22.21563],
      [28, 21.44779],
      [29, 20.70262],
      [30, 20],
      [30, 20],
      [35, 20],
      [35, 95],
      [-4, 95],
      [-4, 82.61796]
    ];
    // Temperature/humidity as point indicator
    this.pointHumidity = fc
      .seriesSvgPoint()
      .crossValue((d) => d[0])
      .mainValue((d) => d[1])
      .decorate((sel) => sel.enter().classed('dotHumidity', true));
    // Grid lines
    this.humidityGridLines = fc
      .annotationSvgGridline()
      .yTicks(10)
      .xTicks(10)
      .xDecorate((sel) => sel.enter().classed('xHumidityGridLines', true))
      .yDecorate((sel) => sel.enter().classed('yHumidityGridLines', true));
    // Combine all chart elements in multi series
    this.multiHumiditySeries = fc
      .seriesSvgMulti()
      .series([this.humidityGridLines, this.pointHumidity]);
    // Define humidity chart
    this.humidityChart = fc
      .chartCartesian(d3.scaleLinear(), d3.scaleLinear())
      .yOrient('left')
      .yDomain([0, 100])
      .xDomain([-10, 40])
      .svgPlotArea(this.multiHumiditySeries)
      .xLabel('Temperature [°C]')
      .yLabel('Humidity [%]')
      .xTicks(10)
      .yTicks(10)
      .decorate((selection) => {
        const xScale = d3.scaleLinear().domain([-10, 40]);
        const yScale = d3.scaleLinear().domain([0, 100]);
        // Get size of plot-area
        selection.select('.plot-area').on('measure', (event) => {
          const { width, height } = event.detail;
          xScale.range([0, width]);
          yScale.range([height, 0]);
          // Remove windows to allow resizing with browser window
          selection.select('#window0dB').remove();
          selection.select('#window10dB').remove();
          selection.select('#window12dB').remove();
        });

        // Draw limitation windows
        selection.on('draw', (event) => {
          const line = fc
            .seriesSvgLine()
            .xScale(xScale)
            .yScale(yScale)
            .crossValue((d) => d[0])
            .mainValue((d) => d[1])
            .decorate((sel) => sel.enter().attr('stroke', ''));
          // Plot 12dB window
          selection
            .select('.plot-area')
            .select('svg')
            .append('g')
            .datum(this.window12dB)
            .call(line)
            .attr('id', 'window12dB')
            .lower();
          // Plot 10dB window
          selection
            .select('.plot-area')
            .select('svg')
            .append('g')
            .datum(this.window10dB)
            .call(line)
            .attr('id', 'window10dB')
            .lower();
          // Plot 0dB window
          selection
            .select('.plot-area')
            .select('svg')
            .append('g')
            .datum(this.window0dB)
            .call(line)
            .attr('id', 'window0dB')
            .lower();
        });
      });
    // Draw chart first time
    d3.select(this.element)
      .datum([[temperature, humidity]])
      .call(this.humidityChart);
  }

  // Update chart with new values
  Update(temperature, humidity) {
    d3.select(this.element)
      .datum([[temperature, humidity]])
      .call(this.humidityChart);
  }
}
