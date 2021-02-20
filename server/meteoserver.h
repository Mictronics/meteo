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

#ifndef METEOSERVER_H
#define METEOSERVER_H

#define SERVER_CMD_START 0x1a
#define SERVER_CMD_STOP 0x2b
#define SERVER_CMD_FROMTO 0x3c
#define SERVER_CMD_ELEVATION 0x4d
#define SERVER_CMD_HEADING 0x5e

typedef struct __attribute__((__packed__))
{
    double gps_hdop;
    double gps_pdop;
    double gps_lat;
    double gps_lon;
    double gps_alt_msl;
    double temperature;
    double baro_pressure;
    double windspeed;
    double windspeed_mean;
    double cross_windspeed;
    double cross_windspeed_mean;
    double head_windspeed;
    double baro_qfe;
    double baro_qnh;
    double gps_time;
    double local_time;
    unsigned short flight_number;
    unsigned short runway_heading;
    unsigned short runway_elevation;
    unsigned short wind_direction;
    unsigned short wind_direction_mean;
    unsigned char barometer_height;
    unsigned char maws_hour;
    unsigned char maws_min;
    unsigned char maws_sec;
    unsigned char humidity;
    unsigned char top_number;
    unsigned char gps_status;
    unsigned char gps_mode;
    unsigned char gps_satellites_visible;
    unsigned char gps_satellites_used;
    unsigned char record_status;
    unsigned char from_to_status;
} t_packet_data;

typedef struct __attribute__((__packed__))
{
    unsigned char id;
    unsigned short flight_number;
    unsigned char top_number;
} t_start_cmd;

typedef struct __attribute__((__packed__))
{
    unsigned char id;
    unsigned short val;
} t_ushort_cmd;

#endif /* METEOSERVER_H */