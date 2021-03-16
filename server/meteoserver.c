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

#include <libwebsockets.h>
#include <pthread.h>
#include <string.h>
#include <getopt.h>
#include <syslog.h>
#include <gps.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <sys/reboot.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "timespec.h"
#include "help.h"
#include "serial.h"
#include "timer.h"
#include "meteoserver.h"

//#define TESTSERIAL

#ifdef TESTSERIAL
FILE *test_fp;
char test_buf[1024];
size_t test_timer = 0;
#endif

#define NOTUSED(V) ((void)V)
#define WSBUFFERSIZE 512 /* Byte */
// See ICAO doc 7488
#define EARTH_G 9.80665
#define R 287.05287
#define TREF 288.15
#define ALPHA -0.0065
#define MOVING_AVG_LENGTH 30 // Moving average length in seconds

static int debug_level = 0;
static int uid = -1, gid = -1, num_clients = 0;
static char interface_name[255] = "";
static const char *iface = NULL;
static int syslog_options = LOG_PID | LOG_PERROR;
static size_t packet_timer = 0;
static size_t record_timer = 0;
static t_packet_data packet_data;
#if GPSD_API_MAJOR_VERSION < 9
static struct timespec ts_now, ts_diff, ts_gps;
#else
static timespec_t ts_now, ts_diff, ts_gps;
#endif

#ifndef LWS_NO_DAEMONIZE
static int daemonize = 0;
#endif
static struct lws_context *context;
static struct lws_context_creation_info info;
static unsigned char wsbuffer[WSBUFFERSIZE];
static unsigned char *pwsbuffer = wsbuffer;
static int wsbuffer_len = 0;
pthread_mutex_t lock_established_conns;
pthread_t gps_thread;
pthread_t serial_thread;
pthread_mutex_t lock_packetdata_update;
static bool gps_thread_exit = false;
static bool serial_thread_exit = false;

/**
 * One of these is created for each client connecting.
 */
struct per_session_data
{
    struct per_session_data *pss_list;
    struct lws *wsi;
    char publishing; // nonzero: peer is publishing to us
};

/**
 *  One of these is created for each vhost our protocol is used with.
 */
struct per_vhost_data
{
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;
    struct per_session_data *pss_list; // linked-list of live pss
};

static void *serial_read_thread(void *arg);
static error_t parse_opt(int key, char *arg, struct argp_state *state);
const char *argp_program_version = "meteoserver v1.0";
const char args_doc[] = "";
const char doc[] = "Websocket Server for Vaisalla weather station\nLicense GPL-3+\n(C) 2021 Michael Wolf\n"
                   "This server provides weather and GPS data to web application via websocket.";
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

/* GPS data source */
static struct
{
    char *spec;
    char *server;
    char *port;
    char *device;
} gpssource;
static struct gps_data_t gpsdata;
static bool gps_available = true;

static FILE *record_fp;

// Storage for moving average over 30s
static double temperature_arr[MOVING_AVG_LENGTH];
static unsigned char humidity_arr[MOVING_AVG_LENGTH];
static double windspeed_arr[MOVING_AVG_LENGTH];
static double cross_wind_arr[MOVING_AVG_LENGTH];
static double head_wind_arr[MOVING_AVG_LENGTH];
static double wind_comp1_arr[MOVING_AVG_LENGTH];
static double wind_comp2_arr[MOVING_AVG_LENGTH];
static unsigned short wind_direction_arr[MOVING_AVG_LENGTH];
static unsigned char moving_avg_index = 0;

static unsigned short runway_elevation = 1204; // Elevation[ft](Manching)
static unsigned char height_qfe = 1;           // Height difference between barometer and reference level[m]
static unsigned short runway_heading = 248;    // Runway heading[deg](Manching)

/**
 * Function parsing the arguments provided on run
 */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    switch (key)
    {
    case 'i':
        strncpy(interface_name, arg, sizeof interface_name);
        interface_name[(sizeof interface_name) - 1] = '\0';
        iface = interface_name;
        break;
    case 'p':
        info.port = atoi(arg);
        break;
    case OPTSERIAL:
        set_serial_interface(arg);
        break;
    case OPTBAUDRATE:
        set_baudrate(arg);
        break;
    case OPTGROUP:
        gid = atoi(arg);
        break;
    case OPTUSER:
        uid = atoi(arg);
        break;
#ifndef LWS_NO_DAEMONIZE
    case 'D':
        daemonize = 1;
        syslog_options &= ~LOG_PERROR;
        break;
#endif
    case 'd':
        debug_level = atoi(arg);
        break;
    case OPTNOGPS:
        gps_available = false;
        lwsl_notice("GPS disabled.\n");
        break;
    case ARGP_KEY_END:
        if (state->arg_num > 0)
            /* We use only options but no arguments */
            argp_usage(state);
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/**
 * Start recording.
 */
static void start_recording(t_start_cmd *start_cmd)
{
    char path[FILENAME_MAX];
    struct stat st = {0};
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    pthread_mutex_trylock(&lock_packetdata_update);
    if (start_cmd->flight_number > 0)
    {
        packet_data.flight_number = start_cmd->flight_number;
    }

    if (start_cmd->top_number >= packet_data.top_number)
    {
        packet_data.top_number = start_cmd->top_number;
    }
    // Create subfolder by date if not exists
    snprintf(path, FILENAME_MAX, "/var/meteodata/%02u%02u%04u", t->tm_mday, t->tm_mon + 1, 1900 + t->tm_year);
    if (stat(path, &st) == -1)
    {
        mkdir(path, 0755);
    }
    // Create file by flight and top number
    snprintf(path, FILENAME_MAX, "/var/meteodata/%02u%02u%04u/F%04u_REC%03u_MeteoData_%02u%02u%02u.csv",
             t->tm_mday,
             t->tm_mon + 1,
             1900 + t->tm_year,
             packet_data.flight_number,
             packet_data.top_number,
             t->tm_hour,
             t->tm_min,
             t->tm_sec);
    record_fp = fopen(path, "a");
    if (record_fp != NULL)
    {
        // Add file header
        fputs("LOG_TIME;TEMP;HUM;PRESSURE;DIRECTION;WIND_TOTAL;WIND_LAT;MEAN_WIND_TOTAL;MEAN_WIND_LAT;GPS_EPOCH_RECORDED;TIME_MAWS_RECORDED;TOP_NUMBER\n", record_fp);
        fputs("HH:MM:SS;degC;%;mbar;deg;kt;kt;kt;kt;seconds;HH:MM:SS;#\n", record_fp);
        packet_data.record_status = 1;
    }
    else
    {
        lwsl_err("Error creating log file: %s\n", strerror(errno));
    }
    pthread_mutex_unlock(&lock_packetdata_update);
}

/**
 * Stop recording.
 */
static void stop_recording(void)
{
    pthread_mutex_trylock(&lock_packetdata_update);
    if (record_fp != NULL)
    {
        fclose(record_fp);
        packet_data.top_number += 1;
    }
    packet_data.record_status = 0;
    pthread_mutex_unlock(&lock_packetdata_update);
}

/**
 * Sync MAWS time with GPS time.
 */
static void sync_maws_time(void)
{
    char *buf;
    ssize_t len = 0;
    // Stop meteo data reception
    serial_thread_exit = true;
    pthread_join(serial_thread, NULL); /* Wait on serial read thread exit */
    // Reopen serial connection to MAWS
    if (open_serial() != EXIT_FAILURE)
    {
        // Open service connection to MAWS
        if (write_serial("open\r\n", 6) != -1)
        {
            sleep(1);
            buf = read_serial(&len);
            // Check for command prompt
            if (buf != NULL && len > 0 && strchr(buf, '>') != NULL)
            {
                pthread_mutex_trylock(&lock_packetdata_update);
                time_t now = (time_t)packet_data.gps_time;
                struct tm *t = gmtime(&now);
                pthread_mutex_unlock(&lock_packetdata_update);
                // Sync GPS time with MAWS
                strftime(buf, 1024, "time %H %M %S %y %m %d\r\n", t);
                write_serial(buf, 24);
                sleep(1);
                // Set UTC time zone in MAWS
                write_serial("timezone 0\r\n", 12);
                sleep(1);
                // Close service connection
                write_serial("close\r\n", 7);
                lwsl_info("GPS time synced to MAWS\n");
            } else {
                lwsl_err("Opening MAWS service connection failed\n");
            }
        }
        close_serial();
    }
    else
    {
        lwsl_err("Serial device init failed\n");
    }
    /* Restart reading serial data from weather station */
    serial_thread_exit = false;
    pthread_create(&serial_thread, NULL, serial_read_thread, NULL);
}

/**
 * Handle requests from client.
 */
static void handle_client_request(void *in, size_t len)
{
    unsigned char *id = (unsigned char *)in;
    t_ushort_cmd *p = (t_ushort_cmd *)in;

    switch (*id)
    {
    case SERVER_CMD_START:
        // Start recording
        if (len < 4)
            break;
        start_recording((t_start_cmd *)in);
        break;
    case SERVER_CMD_STOP:
        // Stop recording
        if (len < 1)
            break;
        stop_recording();
        break;
    case SERVER_CMD_FROMTO:
        // Change runway heading from to status
        if (len < 1)
            break;
        runway_heading = (runway_heading + 180) % 360;
        pthread_mutex_trylock(&lock_packetdata_update);
        if (packet_data.from_to_status == 0)
        {
            packet_data.from_to_status = 1;
        }
        else
        {
            packet_data.from_to_status = 0;
        }
        pthread_mutex_unlock(&lock_packetdata_update);
        break;
    case SERVER_CMD_ELEVATION:
        // Change runway elevation
        if (len < 3)
            break;
        runway_elevation = p->val;
        break;
    case SERVER_CMD_HEADING:
        // Change runway heading
        if (len < 3)
            break;
        runway_heading = p->val;
        break;
    case SERVER_CMD_SYNC_TIME:
        // Sync GPS time to MAWS
        if (len < 1)
            break;
        sync_maws_time();
        break;
    default:
        break;
    }
}

static int callback_broadcast(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len)
{
    struct per_session_data *pss =
        (struct per_session_data *)user;
    struct per_vhost_data *vhd =
        (struct per_vhost_data *)
            lws_protocol_vh_priv_get(lws_get_vhost(wsi),
                                     lws_get_protocol(wsi));
    char buf[32];
    int m;

    switch (reason)
    {
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
        if (num_clients > 50)
        {
            lwsl_notice("50 clients already connected. New connection rejected...\n");
            return -1;
        }
        break;
    case LWS_CALLBACK_PROTOCOL_INIT:
        vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
                                          lws_get_protocol(wsi),
                                          sizeof(struct per_vhost_data));
        vhd->context = lws_get_context(wsi);
        vhd->protocol = lws_get_protocol(wsi);
        vhd->vhost = lws_get_vhost(wsi);
        break;

    case LWS_CALLBACK_PROTOCOL_DESTROY:

        break;

    case LWS_CALLBACK_ESTABLISHED:
        ++num_clients;
        lwsl_notice("Client connected.\n");
        pss->wsi = wsi;
        if (lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI) > 0)
            pss->publishing = !strcmp(buf, "/publisher");
        if (!pss->publishing)
            /* add subscribers to the list of live pss held in the vhd */
            lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
        break;

    case LWS_CALLBACK_CLOSED:
    case LWS_CALLBACK_WSI_DESTROY:
        --num_clients;
        lwsl_notice("Client disconnected.\n");
        /* remove our closing pss from the list of live pss */
        lws_ll_fwd_remove(struct per_session_data, pss_list,
                          pss, vhd->pss_list);
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:

        if (pss->publishing)
            break;

        memcpy(&pwsbuffer[LWS_SEND_BUFFER_PRE_PADDING], (unsigned char *)&packet_data, sizeof(packet_data));
        wsbuffer_len = sizeof(packet_data);

        /* notice we allowed for LWS_PRE in the payload already */
        m = lws_write(wsi, &pwsbuffer[LWS_SEND_BUFFER_PRE_PADDING], wsbuffer_len, LWS_WRITE_BINARY);
        if (m < (int)sizeof(packet_data))
        {
            lwsl_err("ERROR %d writing to ws socket\n", m);
            return -1;
        }
        break;

    case LWS_CALLBACK_RECEIVE:
        /*
		 * For test, our policy is ignore publishing when there are
		 * no subscribers connected.
		 */
        if (!vhd->pss_list)
            break;

        if (len <= 0)
            break;
        pthread_mutex_lock(&lock_established_conns);
        /* Reply 0 on unknown request */
        pwsbuffer[LWS_SEND_BUFFER_PRE_PADDING] = 0;
        wsbuffer_len = 1;
        handle_client_request(in, len);
        pthread_mutex_unlock(&lock_established_conns);

        /*
		 * let every subscriber know we want to write something
		 * on them as soon as they are ready
		 */
        lws_start_foreach_llp(struct per_session_data **, ppss, vhd->pss_list)
        {
            if (!(*ppss)->publishing)
                lws_callback_on_writable((*ppss)->wsi);
        }
        lws_end_foreach_llp(ppss, pss_list);
        break;

    default:
        break;
    }

    return 0;
}

/**
 * Websocket protocol definition.
 */
static struct lws_protocols protocols[] = {
    {"http",
     lws_callback_http_dummy,
     0,
     0,
     0,
     NULL,
     0},
    {"broadcast",
     callback_broadcast,
     sizeof(struct per_session_data),
     WSBUFFERSIZE,
     0, NULL, 0},
    {NULL, NULL, 0, 0, 0, NULL, 0} /* terminator */
};

/**
 * Set affinity of calling thread to specific core on a multi-core CPU
 */
static int thread_to_core(int core_id)
{
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores)
        return EINVAL;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

/**
 * Incoming signal handler. Close server nice and clean.
 */
static void sighandler(int sig)
{
    NOTUSED(sig);
    stop_recording();
    stop_timer(packet_timer);
    stop_timer(record_timer);

#ifdef TESTSERIAL
    stop_timer(test_timer);
#endif

    finalize_timer();

    pthread_mutex_unlock(&lock_packetdata_update);
    gps_thread_exit = true;
    pthread_join(gps_thread, NULL); /* Wait on GPS read thread exit */

    serial_thread_exit = true;
    pthread_join(serial_thread, NULL); /* Wait on serial read thread exit */
    pthread_mutex_destroy(&lock_packetdata_update);

    pthread_mutex_unlock(&lock_established_conns);
    pthread_mutex_destroy(&lock_established_conns);
    lws_cancel_service(context);
    lws_context_destroy(context);

#ifdef TESTSERIAL
    if (test_fp != NULL)
    {
        fclose(test_fp);
    }
#endif

    exit(EXIT_SUCCESS);
}

/**
 * GPS read thread.
 * This need its own thread to avoid delays in time update.
 */
static void *gps_read_thread(void *arg)
{
    NOTUSED(arg);
    thread_to_core(2);
    gpssource.server = (char *)"localhost";
    gpssource.port = (char *)DEFAULT_GPSD_PORT;
    gpssource.device = NULL;

    if (gps_open(gpssource.server, gpssource.port, &gpsdata) != 0)
    {
        gps_available = false;
        lwsl_err("No gpsd running or network error: %s\n", gps_errstr(errno));
        pthread_exit(NULL);
    }

    unsigned int flags = WATCH_ENABLE;
    if (gpssource.device != NULL)
        flags |= WATCH_DEVICE;
    gps_stream(&gpsdata, flags, gpssource.device);

    lwsl_notice("GPS read thread started.");
    while (!gps_thread_exit)
    {
        pthread_mutex_trylock(&lock_packetdata_update);
        if (gps_available)
        {
            if (gps_waiting(&gpsdata, 500000))
            {
#if GPSD_API_MAJOR_VERSION < 9
                if (gps_read(&gpsdata) == -1)
#else
                if (gps_read(&gpsdata, NULL, 0) == -1)
#endif
                {
                    lwsl_err("GPS read error.\n");
                }
                // Calculate difference between local and GPS time
                // Done here to avoid any latency caused by LWS
#if GPSD_API_MAJOR_VERSION < 9
                ts_gps.tv_sec = (long int)gpsdata.fix.time;
#else
                ts_gps = gpsdata.fix.time;
#endif
                (void)clock_gettime(CLOCK_REALTIME, &ts_now);
                TS_SUB(&ts_diff, &ts_now, &ts_gps);
            }
        }
        pthread_mutex_unlock(&lock_packetdata_update);
    }

    gps_close(&gpsdata);
    pthread_mutex_unlock(&lock_packetdata_update);
    pthread_exit(NULL);
}

/**
 * Serial read thread.
 */
static void *serial_read_thread(void *arg)
{
    NOTUSED(arg);
    thread_to_core(3);
    lwsl_notice("Serial read thread started.");
    if (open_serial() == EXIT_FAILURE)
    {
        lwsl_err("Serial device init failed\n");
        serial_thread_exit = true;
    };
    char *buf;
    ssize_t len = 0;
    int items = 0;
    double temperature;
    double temperature_mean = 0.0;
    double pressure;
    double windspeed;
    double windspeed_mean = 0.0;
    double qfe;
    double qnh;
    unsigned short wind_direction;
    double wind_direction_mean = 0.0;
    unsigned char hour;
    unsigned char min;
    unsigned char sec;
    unsigned char humidity;
    double humidity_mean = 0.0;
    signed short difference;
    double cross_wind;
    double head_wind;
    double cross_wind_mean = 0.0;
    double head_wind_mean = 0.0;
    double wind_comp1;
    double wind_comp2;

    while (!serial_thread_exit)
    {
        buf = read_serial(&len);
        if (len > 0)
        {
            items = sscanf(buf, "%lf\t%hhu\t%lf\t%lf\t%hu\t%hhu\t%hhu\t%hhu", &temperature, &humidity, &pressure, &windspeed, &wind_direction, &hour, &min, &sec);
            if (items == 8)
            {
                difference = wind_direction - runway_heading;
                cross_wind = windspeed * sin(difference * DEG_2_RAD);
                head_wind = windspeed * cos(difference * DEG_2_RAD);
                wind_comp1 = windspeed * sin(wind_direction * DEG_2_RAD);
                wind_comp2 = windspeed * cos(wind_direction * DEG_2_RAD);

                // Fill arrays before we calculate average
                if (moving_avg_index < MOVING_AVG_LENGTH)
                {
                    temperature_arr[moving_avg_index] = temperature;
                    humidity_arr[moving_avg_index] = humidity;
                    windspeed_arr[moving_avg_index] = windspeed;
                    wind_direction_arr[moving_avg_index] = wind_direction;
                    cross_wind_arr[moving_avg_index] = cross_wind;
                    head_wind_arr[moving_avg_index] = head_wind;
                    wind_comp1_arr[moving_avg_index] = wind_comp1;
                    wind_comp2_arr[moving_avg_index] = wind_comp2;
                    moving_avg_index += 1;
                }
                else
                {
                    // Left shift all arrays
                    memmove(&temperature_arr[0], &temperature_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                    memmove(&humidity_arr[0], &humidity_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(unsigned char));
                    memmove(&windspeed_arr[0], &windspeed_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                    memmove(&wind_direction_arr[0], &wind_direction_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(unsigned short));
                    memmove(&cross_wind_arr[0], &cross_wind_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                    memmove(&head_wind_arr[0], &head_wind_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                    memmove(&wind_comp1_arr[0], &wind_comp1_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                    memmove(&wind_comp2_arr[0], &wind_comp2_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                    // Add new value at the end
                    temperature_arr[MOVING_AVG_LENGTH - 1] = temperature;
                    humidity_arr[MOVING_AVG_LENGTH - 1] = humidity;
                    windspeed_arr[MOVING_AVG_LENGTH - 1] = windspeed;
                    wind_direction_arr[MOVING_AVG_LENGTH - 1] = wind_direction;
                    cross_wind_arr[MOVING_AVG_LENGTH - 1] = cross_wind;
                    head_wind_arr[MOVING_AVG_LENGTH - 1] = head_wind;
                    wind_comp1_arr[MOVING_AVG_LENGTH - 1] = wind_comp1;
                    wind_comp2_arr[MOVING_AVG_LENGTH - 1] = wind_comp2;
                    // Calculate average
                    for (int i = 0; i < MOVING_AVG_LENGTH; i++)
                    {
                        temperature_mean += temperature_arr[i];
                        humidity_mean += humidity_arr[i];
                        windspeed_mean += windspeed_arr[i];
                        cross_wind_mean += cross_wind_arr[i];
                        head_wind_mean += head_wind_arr[i];
                        wind_comp1 += wind_comp1_arr[i];
                        wind_comp2 += wind_comp2_arr[i];
                    }

                    temperature_mean /= (double)MOVING_AVG_LENGTH;
                    humidity_mean /= (double)MOVING_AVG_LENGTH;
                    windspeed_mean /= (double)MOVING_AVG_LENGTH;
                    cross_wind_mean /= (double)MOVING_AVG_LENGTH;
                    head_wind_mean /= (double)MOVING_AVG_LENGTH;
                    wind_comp1 /= (double)MOVING_AVG_LENGTH;
                    wind_comp2 /= (double)MOVING_AVG_LENGTH;

                    wind_direction_mean = atan2(wind_comp1, wind_comp2) * RAD_2_DEG;
                    if (wind_direction_mean < 0.0)
                    {
                        wind_direction_mean = 360.0 + wind_direction_mean;
                    }
                }

                double elev = runway_elevation * 0.3048;
                qfe = pressure * (1.0 + ((height_qfe * EARTH_G) / (R * (temperature + 273.15))));
                qnh = qfe * exp((elev * EARTH_G) / (R * (TREF + (ALPHA * elev) / 2.0)));

                // Block mutex only minimum time
                pthread_mutex_trylock(&lock_packetdata_update);
                packet_data.baro_qfe = qfe;
                packet_data.baro_qnh = qnh;
                packet_data.temperature = temperature_mean;
                packet_data.humidity = humidity_mean;
                packet_data.wind_direction = wind_direction;
                packet_data.wind_direction_mean = (unsigned short)wind_direction_mean;
                packet_data.windspeed = windspeed;
                packet_data.windspeed_mean = windspeed_mean;
                packet_data.cross_windspeed = (double)cross_wind;
                packet_data.cross_windspeed_mean = (double)cross_wind_mean;
                packet_data.head_windspeed = (double)head_wind_mean;
                packet_data.baro_pressure = pressure;
                packet_data.maws_hour = hour;
                packet_data.maws_min = min;
                packet_data.maws_sec = sec;
                pthread_mutex_unlock(&lock_packetdata_update);
            }
        }
        else if (len < 0)
        {
            lwsl_err("Error from read: %ld: %s\n", len, strerror(errno));
        }
    }

    close_serial();
    pthread_mutex_unlock(&lock_packetdata_update);
    pthread_exit(NULL);
}

/**
 * Callback function for packet timer.
 * Send packet data periodically to clients.
 */
static void packet_timer_handler(size_t timer_id, void *user_data)
{
    NOTUSED(timer_id);
    NOTUSED(user_data);
    // Block gpsdata only a minimum time
    pthread_mutex_trylock(&lock_packetdata_update);
    packet_data.gps_status = (unsigned char)gpsdata.status;
    packet_data.gps_mode = (unsigned char)gpsdata.fix.mode;
    packet_data.gps_satellites_visible = (unsigned char)gpsdata.satellites_visible;
    packet_data.gps_satellites_used = (unsigned char)gpsdata.satellites_used;
    packet_data.gps_hdop = gpsdata.dop.hdop;
    packet_data.gps_pdop = gpsdata.dop.pdop;
    packet_data.gps_lat = gpsdata.fix.latitude;
    packet_data.gps_lon = gpsdata.fix.longitude;
#if GPSD_API_MAJOR_VERSION < 9
    packet_data.gps_alt_msl = gpsdata.fix.altitude * METERS_TO_FEET;
    packet_data.gps_time = gpsdata.fix.time;
#else
    packet_data.gps_alt_msl = gpsdata.fix.altMSL * METERS_TO_FEET;
    packet_data.gps_time = (double)gpsdata.fix.time.tv_sec;
#endif
    packet_data.runway_elevation = runway_elevation;
    packet_data.runway_heading = runway_heading;
    pthread_mutex_unlock(&lock_packetdata_update);

    lws_callback_on_writable_all_protocol(context, &protocols[1]);
}

/**
 * Callback function for record timer.
 * Log data packet into file if record is active.
 */
static void record_timer_handler(size_t timer_id, void *user_data)
{
    NOTUSED(timer_id);
    NOTUSED(user_data);
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    if (record_fp != NULL && packet_data.record_status != 0)
    {
        pthread_mutex_trylock(&lock_packetdata_update);
        fprintf(record_fp, "%02u:%02u:%02u;%0.1f;%u;%0.1f;%u;%0.1f;%0.1f;%0.1f;%0.1f;%.0f;%02u:%02u:%02u;%u\n",
                t->tm_hour,
                t->tm_min,
                t->tm_sec,
                packet_data.temperature,
                packet_data.humidity,
                packet_data.baro_pressure,
                packet_data.wind_direction,
                packet_data.windspeed,
                packet_data.cross_windspeed,
                packet_data.windspeed_mean,
                packet_data.cross_windspeed_mean,
                packet_data.gps_time,
                packet_data.maws_hour,
                packet_data.maws_min,
                packet_data.maws_sec,
                packet_data.top_number);
        pthread_mutex_unlock(&lock_packetdata_update);
    }
}

#ifdef TESTSERIAL
static void test_handler(size_t timer_id, void *user_data)
{
    NOTUSED(timer_id);
    NOTUSED(user_data);

    char *buf;
    ssize_t len = 0;
    int items = 0;
    double temperature;
    double temperature_mean = 0.0;
    double pressure;
    double windspeed;
    double windspeed_mean = 0.0;
    double qfe;
    double qnh;
    unsigned short wind_direction;
    double wind_direction_mean = 0.0;
    unsigned char hour;
    unsigned char min;
    unsigned char sec;
    unsigned char humidity;
    double humidity_mean = 0.0;
    signed short difference;
    double cross_wind;
    double head_wind;
    double cross_wind_mean = 0.0;
    double head_wind_mean = 0.0;
    double wind_comp1;
    double wind_comp2;

    if (test_fp == NULL || fgets(test_buf, 1024, test_fp) == NULL)
    {
        return;
    }

    buf = test_buf;
    len = 52;
    if (len > 0)
    {
        items = sscanf(buf, "%lf\t%hhu\t%lf\t%lf\t%hu\t%hhu\t%hhu\t%hhu", &temperature, &humidity, &pressure, &windspeed, &wind_direction, &hour, &min, &sec);
        if (items == 8)
        {
            difference = wind_direction - runway_heading;
            cross_wind = windspeed * sin(difference * DEG_2_RAD);
            head_wind = windspeed * cos(difference * DEG_2_RAD);
            wind_comp1 = windspeed * sin(wind_direction * DEG_2_RAD);
            wind_comp2 = windspeed * cos(wind_direction * DEG_2_RAD);

            // Fill arrays before we calculate average
            if (moving_avg_index < MOVING_AVG_LENGTH)
            {
                temperature_arr[moving_avg_index] = temperature;
                humidity_arr[moving_avg_index] = humidity;
                windspeed_arr[moving_avg_index] = windspeed;
                wind_direction_arr[moving_avg_index] = wind_direction;
                cross_wind_arr[moving_avg_index] = cross_wind;
                head_wind_arr[moving_avg_index] = head_wind;
                wind_comp1_arr[moving_avg_index] = wind_comp1;
                wind_comp2_arr[moving_avg_index] = wind_comp2;
                moving_avg_index += 1;
            }
            else
            {
                // Left shift all arrays
                memmove(&temperature_arr[0], &temperature_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                memmove(&humidity_arr[0], &humidity_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(unsigned char));
                memmove(&windspeed_arr[0], &windspeed_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                memmove(&wind_direction_arr[0], &wind_direction_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(unsigned short));
                memmove(&cross_wind_arr[0], &cross_wind_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                memmove(&head_wind_arr[0], &head_wind_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                memmove(&wind_comp1_arr[0], &wind_comp1_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                memmove(&wind_comp2_arr[0], &wind_comp2_arr[1], (MOVING_AVG_LENGTH - 1) * sizeof(double));
                // Add new value at the end
                temperature_arr[MOVING_AVG_LENGTH - 1] = temperature;
                humidity_arr[MOVING_AVG_LENGTH - 1] = humidity;
                windspeed_arr[MOVING_AVG_LENGTH - 1] = windspeed;
                wind_direction_arr[MOVING_AVG_LENGTH - 1] = wind_direction;
                cross_wind_arr[MOVING_AVG_LENGTH - 1] = cross_wind;
                head_wind_arr[MOVING_AVG_LENGTH - 1] = head_wind;
                wind_comp1_arr[MOVING_AVG_LENGTH - 1] = wind_comp1;
                wind_comp2_arr[MOVING_AVG_LENGTH - 1] = wind_comp2;
                // Calculate average
                for (int i = 0; i < MOVING_AVG_LENGTH; i++)
                {
                    temperature_mean += temperature_arr[i];
                    humidity_mean += humidity_arr[i];
                    windspeed_mean += windspeed_arr[i];
                    cross_wind_mean += cross_wind_arr[i];
                    head_wind_mean += head_wind_arr[i];
                    wind_comp1 += wind_comp1_arr[i];
                    wind_comp2 += wind_comp2_arr[i];
                }

                temperature_mean /= (double)MOVING_AVG_LENGTH;
                humidity_mean /= (double)MOVING_AVG_LENGTH;
                windspeed_mean /= (double)MOVING_AVG_LENGTH;
                cross_wind_mean /= (double)MOVING_AVG_LENGTH;
                head_wind_mean /= (double)MOVING_AVG_LENGTH;
                wind_comp1 /= (double)MOVING_AVG_LENGTH;
                wind_comp2 /= (double)MOVING_AVG_LENGTH;

                wind_direction_mean = atan2(wind_comp1, wind_comp2) * RAD_2_DEG;
                if (wind_direction_mean < 0.0)
                {
                    wind_direction_mean = 360.0 + wind_direction_mean;
                }
            }

            double elev = runway_elevation * 0.3048;
            qfe = pressure * (1.0 + ((height_qfe * EARTH_G) / (R * (temperature + 273.15))));
            qnh = qfe * exp((elev * EARTH_G) / (R * (TREF + (ALPHA * elev) / 2.0)));

            // Block mutex only minimum time
            pthread_mutex_trylock(&lock_packetdata_update);
            packet_data.baro_qfe = qfe;
            packet_data.baro_qnh = qnh;
            packet_data.temperature = temperature_mean;
            packet_data.humidity = humidity_mean;
            packet_data.wind_direction = wind_direction;
            packet_data.wind_direction_mean = (unsigned short)wind_direction_mean;
            packet_data.windspeed = windspeed;
            packet_data.windspeed_mean = windspeed_mean;
            packet_data.cross_windspeed = (double)cross_wind;
            packet_data.cross_windspeed_mean = (double)cross_wind_mean;
            packet_data.head_windspeed = (double)head_wind_mean;
            packet_data.baro_pressure = pressure;
            packet_data.maws_hour = hour;
            packet_data.maws_min = min;
            packet_data.maws_sec = sec;
            pthread_mutex_unlock(&lock_packetdata_update);
        }
    }
    else if (len < 0)
    {
        lwsl_err("Error from read: %ld: %s\n", len, strerror(errno));
    }
}
#endif

/**
 * Well, it's main.
 */
int main(int argc, char **argv)
{
    memset(&packet_data, 0, sizeof(packet_data));
    packet_data.runway_elevation = runway_elevation;
    packet_data.runway_heading = runway_heading;
    packet_data.barometer_height = height_qfe;
    packet_data.baro_qfe = 1013.25;
    packet_data.baro_qnh = 1013.25;

    /* On a multi-core CPU we run the main thread and reader thread on different cores.
     * Try sticking the main thread to core 1
     */
    thread_to_core(1);

    /* Take care to zero down the info struct, contains random garbage
     * from the stack otherwise.
     */
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.iface = "127.0.0.1";
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.max_http_header_pool = 16;
    info.options = 0;
    info.extensions = NULL;
    info.timeout_secs = 5;
    info.max_http_header_data = 2048;
    /* Parse the command line options */
    if (argp_parse(&argp, argc, argv, 0, 0, 0))
    {
        exit(EXIT_SUCCESS);
    }

#if !defined(LWS_NO_DAEMONIZE)
    /* Normally lock path would be /var/lock/lwsts or similar, to
     * simplify getting started without having to take care about
     * permissions or running as root, set to /tmp/.lwsts-lock
     */
    if (daemonize && lws_daemonize("/tmp/.lwsts-lock"))
    {
        lwsl_err("Failed to daemonize\n");
        return EXIT_FAILURE;
    }
#endif

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGQUIT, sighandler);

    /* Only try to log things according to our debug_level */
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("lwsts", syslog_options, LOG_DAEMON);

    /* Tell the library what debug level to emit and to send it to syslog */
    lws_set_log_level(debug_level, lwsl_emit_syslog);

    info.iface = iface;
    info.gid = gid;
    info.uid = uid;
    info.max_http_header_pool = 16;
    info.timeout_secs = 5;

    /* Create libwebsocket context representing this server */
    context = lws_create_context(&info);

    if (context == NULL)
    {
        lwsl_err("libwebsocket init failed\n");
        return EXIT_FAILURE;
    }

    /* Start reading serial data from weather station */
    pthread_create(&serial_thread, NULL, serial_read_thread, NULL);

    /* Initialize GPS data so we read back zero if no GPS is available */
    memset(&gpsdata, 0, sizeof(gpsdata));
    if (gps_available)
    {
        /* Start reading GPS data */
        pthread_create(&gps_thread, NULL, gps_read_thread, NULL);
    }

    initialize_timer();
    packet_timer = start_timer(500, packet_timer_handler, TIMER_PERIODIC, NULL);
    record_timer = start_timer(1000, record_timer_handler, TIMER_PERIODIC, NULL);

#ifdef TESTSERIAL
    test_fp = fopen("/tmp/vaisalla_log.txt", "r");
    test_timer = start_timer(1000, test_handler, TIMER_PERIODIC, NULL);
#endif

    // Check if serial thread is running.
    // Exit if not, e.g. serial interface not open.
    if (serial_thread_exit)
    {
        sighandler(0);
    }

    // Infinite loop, to end this server send SIGTERM. (CTRL+C) */
    for (;;)
    {
        lws_service(context, 100);
        /* libwebsocket_service will process all waiting events with
         * their callback functions and then wait 100 ms.
         * (This is a single threaded web server and this will keep our
         * server from generating load while there are not
         * requests to process)
         */
    }

    sighandler(0);

    return EXIT_SUCCESS;
}
