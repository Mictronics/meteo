#ifndef METEOSERVER_H
#define METEOSERVER_H

#define SERVER_CMD_SET_MOTORS 0x10
#define SERVER_CMD_SET_SERVOS 0x20
#define SERVER_CMD_BATTERY_LEVEL 0x30
#define SERVER_CMD_PING 0x55

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
    unsigned char gps_satellites_visible;
    unsigned char gps_satellites_used;
    unsigned char record_status;
} t_packet_data;

#endif /* METEOSERVER_H */