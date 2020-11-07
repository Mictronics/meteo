#ifndef METEOSERVER_H
#define METEOSERVER_H

#define SERVER_CMD_START 0x1a
#define SERVER_CMD_STOP 0x2b

typedef struct __attribute__((__packed__))
{
    double gps_hdop;
    double gps_pdop;
    double gps_lat;
    double gps_lon;
    double gps_alt_msl;
    double barometer_height;
    double runway_elevation;
    double temperature;
    double baro_pressure;
    double windspeed;
    double cross_windspeed;
    double head_windspeed;
    double baro_qfe;
    double baro_qnh;
    long gps_time;
    long tm_diff_sec;
    long tm_diff_nsec;
    unsigned short flight_number;
    unsigned short runway_heading;
    unsigned short wind_direction;
    unsigned short tm_year; /* Year	- 1900.  */
    unsigned char tm_mon;   /* Month.	[0-11] */
    unsigned char tm_mday;  /* Day.		[1-31] */
    unsigned char tm_hour;  /* Hours.	[0-23] */
    unsigned char tm_min;   /* Minutes.	[0-59] */
    unsigned char tm_sec;   /* Seconds.	[0-60] (1 leap second) */
    unsigned char humidity;
    unsigned char top_number;
    unsigned char gps_status;
    unsigned char gps_mode;
    unsigned char gps_satellites_visible;
    unsigned char gps_satellites_used;
    unsigned char record_status;
} t_packet_data;

typedef struct __attribute__((__packed__))
{
    unsigned char id;
    unsigned short flight_number;
    unsigned char top_number;
} t_start_cmd;

#endif /* METEOSERVER_H */