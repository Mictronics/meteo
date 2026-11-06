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

#include <libwebsockets.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <gps.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include "help.h"
#include "serial.h"
#include "average.h"
#include "meteoserver.h"
#include "version.h"

#define NOTUSED(V) ((void)V)
#define MAX_CLIENTS 50
#define MAX_ELEVATION_FT 29030 /* Same limit as the client input */
#define SYNC_EVERY 5 // rows (seconds at 1 Hz) between fdatasync calls

#ifndef RECORD_DIR
#define RECORD_DIR "/mnt/data/meteodata"
#endif
#define PACKET_INTERVAL_US 500000 /* Websocket update rate */
#define RECORD_INTERVAL_US 1000000
// See ICAO doc 7488
#define EARTH_G 9.80665
#define R 287.05287
#define TREF 288.15
#define ALPHA -0.0065

static int debug_level = LLL_ERR | LLL_WARN | LLL_NOTICE;
static int uid = -1, gid = -1, num_clients = 0;
static char interface_name[255] = "";
static const char *iface = NULL;
static int syslog_options = LOG_PID | LOG_PERROR;

#ifndef LWS_NO_DAEMONIZE
static int daemonize = 0;
#endif
static struct lws_context *context;
static struct lws_context_creation_info info;
static lws_sorted_usec_list_t packet_sul;
static lws_sorted_usec_list_t record_sul;

/* Shared between serial, GPS and the websocket (main) thread. Guarded by lock_packetdata. */
static t_packet_data packet_data;
static pthread_mutex_t lock_packetdata = PTHREAD_MUTEX_INITIALIZER;

static pthread_t gps_thread;
static pthread_t serial_thread;
static atomic_bool gps_thread_exit = false;
static atomic_bool serial_thread_exit = false;
static atomic_bool sync_requested = false;
static volatile sig_atomic_t quit_requested = 0;

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

static error_t parse_opt(int key, char *arg, struct argp_state *state);
const char *argp_program_version = "meteoserver " METEO_VERSION;
const char args_doc[] = "";
const char doc[] = "Websocket Server for Vaisalla weather station\nLicense GPL-3+\n(C) 2026 Michael Wolf\n"
                   "This server provides weather and GPS data to web application via websocket.";
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

static struct gps_data_t gpsdata; // Only touched by the GPS thread
static bool gps_available = true;

static FILE *record_fp; // Only touched by the main thread

static const unsigned char height_qfe = 1; // Height difference between barometer and reference level[m]

static long parse_number(const char *arg, long min, long max, struct argp_state *state, const char *name)
{
    char *end;
    errno = 0;
    long v = strtol(arg, &end, 10);
    if (errno != 0 || end == arg || *end != '\0' || v < min || v > max)
        argp_error(state, "invalid %s: '%s'", name, arg);
    return v;
}

/**
 * Function parsing the arguments provided on run
 */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    switch (key)
    {
    case 'i':
        snprintf(interface_name, sizeof interface_name, "%s", arg);
        iface = interface_name;
        break;
    case 'p':
        info.port = (int)parse_number(arg, 1, 65535, state, "port");
        break;
    case OPTSERIAL:
        set_serial_interface(arg);
        break;
    case OPTBAUDRATE:
        set_baudrate(arg);
        break;
    case OPTGROUP:
        gid = (int)parse_number(arg, -1, INT32_MAX, state, "group id");
        break;
    case OPTUSER:
        uid = (int)parse_number(arg, -1, INT32_MAX, state, "user id");
        break;
#ifndef LWS_NO_DAEMONIZE
    case 'D':
        daemonize = 1;
        syslog_options &= ~LOG_PERROR;
        break;
#endif
    case 'd':
        debug_level = (int)parse_number(arg, 0, 0xffff, state, "debug level");
        break;
    case OPTNOGPS:
        gps_available = false;
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
 * Start recording. Runs on the main thread.
 */
static void start_recording(const t_start_cmd *start_cmd)
{
    char path[FILENAME_MAX];
    time_t now = time(NULL);
    struct tm t;
    unsigned short flight_number;
    unsigned char top_number;

    if (record_fp != NULL)
        return; // Already recording

    gmtime_r(&now, &t); // UTC, like the MAWS and GPS time

    pthread_mutex_lock(&lock_packetdata);
    if (start_cmd->flight_number > 0)
    {
        packet_data.flight_number = start_cmd->flight_number;
    }

    if (start_cmd->top_number >= packet_data.top_number)
    {
        packet_data.top_number = start_cmd->top_number;
    }
    flight_number = packet_data.flight_number;
    top_number = packet_data.top_number;
    pthread_mutex_unlock(&lock_packetdata);

    // Create subfolder by date if not exists
    snprintf(path, sizeof path, RECORD_DIR "/%02u%02u%04u", t.tm_mday, t.tm_mon + 1, 1900 + t.tm_year);
    if (mkdir(path, 0755) == -1 && errno != EEXIST)
    {
        lwsl_err("Error creating folder %s: %s\n", path, strerror(errno));
        return;
    }

    // Create file by flight and top number
    snprintf(path, sizeof path, RECORD_DIR "/%02u%02u%04u/F%04u_REC%03u_MeteoData_%02u%02u%02u.csv",
             t.tm_mday,
             t.tm_mon + 1,
             1900 + t.tm_year,
             flight_number,
             top_number,
             t.tm_hour,
             t.tm_min,
             t.tm_sec);
    record_fp = fopen(path, "a");
    if (record_fp == NULL)
    {
        lwsl_err("Error creating log file: %s\n", strerror(errno));
        return;
    }

    fputs("LOG_TIME;TEMP;HUM;PRESSURE;DIRECTION;WIND_TOTAL;WIND_LAT;MEAN_WIND_TOTAL;MEAN_WIND_LAT;GPS_EPOCH_RECORDED;TIME_MAWS_RECORDED;TOP_NUMBER\n", record_fp);
    fputs("HH:MM:SS;degC;%;mbar;deg;kt;kt;kt;kt;seconds;HH:MM:SS;#\n", record_fp);
    fflush(record_fp);
    fdatasync(fileno(record_fp));

    pthread_mutex_lock(&lock_packetdata);
    packet_data.record_status = 1;
    pthread_mutex_unlock(&lock_packetdata);
}

/**
 * Stop recording. Runs on the main thread.
 */
static void stop_recording(void)
{
    bool was_recording = (record_fp != NULL);

    if (was_recording)
    {
        fflush(record_fp);
        fdatasync(fileno(record_fp));
        fclose(record_fp);
        record_fp = NULL;
    }

    pthread_mutex_lock(&lock_packetdata);
    if (was_recording)
    {
        packet_data.top_number += 1;
    }
    packet_data.record_status = 0;
    pthread_mutex_unlock(&lock_packetdata);
}

static void write_str(const char *s)
{
    write_serial(s, strlen(s));
}

/**
 * Sync MAWS time with GPS time. Runs on the serial thread, which owns the serial port.
 */
static void sync_maws_time(void)
{
    char *buf;
    ssize_t len = 0;
    bool service = false;

    // Reopen serial connection to MAWS
    close_serial();
    if (open_serial() == EXIT_FAILURE)
    {
        lwsl_err("Serial device init failed\n");
        return;
    }

    // Open service connection to MAWS
    write_str("open\r\n");
    sleep(1);
    // Read lines until we find service notification
    for (int i = 0; i < 10 && !service; i++)
    {
        buf = read_serial(&len);
        service = (buf != NULL && len > 0 && strstr(buf, "Service") != NULL);
    }

    if (service)
    {
        char cmd[64];
        struct tm t;

        pthread_mutex_lock(&lock_packetdata);
        time_t now = (time_t)packet_data.gps_time;
        pthread_mutex_unlock(&lock_packetdata);
        gmtime_r(&now, &t);

        // Sync GPS time with MAWS
        strftime(cmd, sizeof cmd, "time %H %M %S %y %m %d\r\n", &t);
        write_str(cmd);
        sleep(1);
        // Set UTC time zone in MAWS
        write_str("timezone 0\r\n");
        sleep(1);
        lwsl_notice("GPS time synced to MAWS\n");
    }
    else
    {
        lwsl_err("Opening MAWS service connection failed\n");
    }

    // Try to close service connection in any case, then start over with a clean port
    write_str("close\r\n");
    close_serial();
    open_serial();
}

/**
 * Handle requests from client.
 */
static void handle_client_request(const void *in, size_t len)
{
    const unsigned char *id = (const unsigned char *)in;
    const t_ushort_cmd *p = (const t_ushort_cmd *)in;

    switch (*id)
    {
    case SERVER_CMD_START:
        // Start recording
        if (len < sizeof(t_start_cmd))
            break;
        start_recording((const t_start_cmd *)in);
        break;
    case SERVER_CMD_STOP:
        // Stop recording
        stop_recording();
        break;
    case SERVER_CMD_FROMTO:
        // Change runway heading from to status
        pthread_mutex_lock(&lock_packetdata);
        packet_data.runway_heading = (packet_data.runway_heading + 180) % 360;
        packet_data.from_to_status = !packet_data.from_to_status;
        pthread_mutex_unlock(&lock_packetdata);
        break;
    case SERVER_CMD_ELEVATION:
        // Change runway elevation
        if (len < sizeof(t_ushort_cmd) || p->val > MAX_ELEVATION_FT)
            break;
        pthread_mutex_lock(&lock_packetdata);
        packet_data.runway_elevation = p->val;
        pthread_mutex_unlock(&lock_packetdata);
        break;
    case SERVER_CMD_HEADING:
        // Change runway heading
        if (len < sizeof(t_ushort_cmd) || p->val > 359)
            break;
        pthread_mutex_lock(&lock_packetdata);
        packet_data.runway_heading = p->val;
        pthread_mutex_unlock(&lock_packetdata);
        break;
    case SERVER_CMD_SYNC_TIME:
    {
        // Sync GPS time to MAWS, done by the serial thread
        pthread_mutex_lock(&lock_packetdata);
        bool gps_valid = (packet_data.gps_mode > 1 && packet_data.gps_time > 0.0);
        pthread_mutex_unlock(&lock_packetdata);
        if (gps_valid)
            atomic_store(&sync_requested, true);
        else
            lwsl_err("No valid GPS time, MAWS sync ignored\n");
        break;
    }
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
        if (num_clients >= MAX_CLIENTS)
        {
            lwsl_notice("%d clients already connected. New connection rejected...\n", MAX_CLIENTS);
            return -1;
        }
        break;
    case LWS_CALLBACK_PROTOCOL_INIT:
        vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
                                          lws_get_protocol(wsi),
                                          sizeof(struct per_vhost_data));
        if (vhd == NULL)
            return -1;
        vhd->context = lws_get_context(wsi);
        vhd->protocol = lws_get_protocol(wsi);
        vhd->vhost = lws_get_vhost(wsi);
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
        if (pss == NULL || vhd == NULL)
            break;
        --num_clients;
        lwsl_notice("Client disconnected.\n");
        /* remove our closing pss from the list of live pss */
        lws_ll_fwd_remove(struct per_session_data, pss_list,
                          pss, vhd->pss_list);
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
    {
        unsigned char out[LWS_PRE + sizeof(packet_data)];

        if (pss->publishing)
            break;

        pthread_mutex_lock(&lock_packetdata);
        memcpy(&out[LWS_PRE], &packet_data, sizeof(packet_data));
        pthread_mutex_unlock(&lock_packetdata);

        m = lws_write(wsi, &out[LWS_PRE], sizeof(packet_data), LWS_WRITE_BINARY);
        if (m < (int)sizeof(packet_data))
        {
            lwsl_err("ERROR %d writing to ws socket\n", m);
            return -1;
        }
        break;
    }

    case LWS_CALLBACK_RECEIVE:
        /*
         * For test, our policy is ignore publishing when there are
         * no subscribers connected.
         */
        if (vhd == NULL || !vhd->pss_list)
            break;

        if (len == 0)
            break;
        handle_client_request(in, len);

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
     512,
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
 * Incoming signal handler. Only flags the main loop, which shuts down cleanly.
 */
static void sighandler(int sig)
{
    NOTUSED(sig);
    quit_requested = 1;
}

static double finite_or_zero(double v)
{
    return isfinite(v) ? v : 0.0;
}

#define GPS_STALE_SEC 5 // No gpsd message for this long: drop the last fix

/**
 * Zero the GPS fields so clients and the MAWS time sync never see a frozen fix.
 */
static void clear_gps_data(void)
{
    pthread_mutex_lock(&lock_packetdata);
    packet_data.gps_status = 0;
    packet_data.gps_mode = 0;
    packet_data.gps_satellites_visible = 0;
    packet_data.gps_satellites_used = 0;
    packet_data.gps_hdop = 0;
    packet_data.gps_pdop = 0;
    packet_data.gps_lat = 0;
    packet_data.gps_lon = 0;
    packet_data.gps_alt_msl = 0;
    packet_data.gps_time = 0;
    pthread_mutex_unlock(&lock_packetdata);
}

/**
 * GPS read thread.
 * This need its own thread to avoid delays in time update.
 */
static void *gps_read_thread(void *arg)
{
    NOTUSED(arg);
    thread_to_core(2);
    bool connected = false;
    bool warned = false;
    struct timespec last_msg;
    bool stale = true;

    lwsl_notice("GPS read thread started.\n");
    while (!gps_thread_exit)
    {
        if (!connected)
        {
            if (gps_open("localhost", DEFAULT_GPSD_PORT, &gpsdata) != 0)
            {
                if (!warned)
                    lwsl_err("No gpsd running or network error: %s\n", gps_errstr(errno));
                warned = true;
                if (!stale)
                {
                    clear_gps_data();
                    stale = true;
                }
                sleep(1);
                continue;
            }
            gps_stream(&gpsdata, WATCH_ENABLE | WATCH_JSON, NULL);
            clock_gettime(CLOCK_MONOTONIC, &last_msg);
            connected = true;
            warned = false;
        }

        if (!gps_waiting(&gpsdata, 500000))
        {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (!stale && now.tv_sec - last_msg.tv_sec >= GPS_STALE_SEC)
            {
                lwsl_warn("No GPS data from gpsd, clearing fix.\n");
                clear_gps_data();
                stale = true;
            }
            continue;
        }

#if GPSD_API_MAJOR_VERSION < 9
        if (gps_read(&gpsdata) == -1)
#else
        if (gps_read(&gpsdata, NULL, 0) == -1)
#endif
        {
            lwsl_err("GPS read error.\n");
            gps_close(&gpsdata);
            connected = false;
            clear_gps_data();
            stale = true;
            sleep(1);
            continue;
        }

        clock_gettime(CLOCK_MONOTONIC, &last_msg);
        stale = false;
        pthread_mutex_lock(&lock_packetdata);
#if GPSD_API_MAJOR_VERSION < 10
        packet_data.gps_status = (unsigned char)gpsdata.status;
#else
        packet_data.gps_status = (unsigned char)gpsdata.fix.status;
#endif
        packet_data.gps_mode = (unsigned char)gpsdata.fix.mode;
        packet_data.gps_satellites_visible = (unsigned char)gpsdata.satellites_visible;
        packet_data.gps_satellites_used = (unsigned char)gpsdata.satellites_used;
        packet_data.gps_hdop = finite_or_zero(gpsdata.dop.hdop);
        packet_data.gps_pdop = finite_or_zero(gpsdata.dop.pdop);
        packet_data.gps_lat = finite_or_zero(gpsdata.fix.latitude);
        packet_data.gps_lon = finite_or_zero(gpsdata.fix.longitude);
#if GPSD_API_MAJOR_VERSION < 9
        packet_data.gps_alt_msl = finite_or_zero(gpsdata.fix.altitude) * METERS_TO_FEET;
        packet_data.gps_time = finite_or_zero(gpsdata.fix.time);
#else
        packet_data.gps_alt_msl = finite_or_zero(gpsdata.fix.altMSL) * METERS_TO_FEET;
        packet_data.gps_time = (double)gpsdata.fix.time.tv_sec;
#endif
        pthread_mutex_unlock(&lock_packetdata);
    }

    if (connected)
        gps_close(&gpsdata);
    return NULL;
}

/**
 * Reopen the serial port after a read error, e.g. unplugged adapter.
 */
static void reopen_serial(void)
{
    close_serial();
    while (!serial_thread_exit)
    {
        usleep(500000);
        if (open_serial() == EXIT_SUCCESS)
        {
            lwsl_notice("Serial device reopened.\n");
            return;
        }
    }
}

/**
 * Serial read thread.
 */
static void *serial_read_thread(void *arg)
{
    NOTUSED(arg);
    thread_to_core(3);
    lwsl_notice("Serial read thread started.\n");

    t_average avg = {0};
    t_mean mean;
    char *buf;
    ssize_t len = 0;

    while (!serial_thread_exit)
    {
        if (atomic_exchange(&sync_requested, false))
            sync_maws_time();

        buf = read_serial(&len);
        if (len < 0)
        {
            lwsl_err("Error from read: %ld: %s\n", (long)len, strerror(errno));
            reopen_serial();
            continue;
        }
        if (len == 0)
        {
            usleep(10000);
            continue;
        }

        double temperature, pressure, windspeed;
        unsigned short wind_direction;
        unsigned char hour, min, sec, humidity;
        int items = sscanf(buf, "%lf\t%hhu\t%lf\t%lf\t%hu\t%hhu\t%hhu\t%hhu", &temperature, &humidity, &pressure, &windspeed, &wind_direction, &hour, &min, &sec);
        if (items != 8)
            continue;

        pthread_mutex_lock(&lock_packetdata);
        unsigned short runway_heading = packet_data.runway_heading;
        unsigned short runway_elevation = packet_data.runway_elevation;
        pthread_mutex_unlock(&lock_packetdata);

        int difference = (int)wind_direction - (int)runway_heading;
        t_sample sample = {
            .temperature = temperature,
            .humidity = humidity,
            .windspeed = windspeed,
            .cross_wind = windspeed * sin(difference * DEG_2_RAD),
            .head_wind = windspeed * cos(difference * DEG_2_RAD),
            .wind_comp1 = windspeed * sin(wind_direction * DEG_2_RAD),
            .wind_comp2 = windspeed * cos(wind_direction * DEG_2_RAD)};
        average_add(&avg, &sample);
        average_get(&avg, &mean);

        double elev = runway_elevation * 0.3048;
        double qfe = pressure * (1.0 + ((height_qfe * EARTH_G) / (R * (temperature + 273.15))));
        double qnh = qfe * exp((elev * EARTH_G) / (R * (TREF + (ALPHA * elev) / 2.0)));

        pthread_mutex_lock(&lock_packetdata);
        packet_data.baro_qfe = qfe;
        packet_data.baro_qnh = qnh;
        packet_data.temperature = mean.temperature;
        packet_data.humidity = (unsigned char)(mean.humidity + 0.5);
        packet_data.wind_direction = wind_direction;
        packet_data.wind_direction_mean = (unsigned short)((int)lround(mean.direction) % 360);
        packet_data.windspeed = windspeed;
        packet_data.windspeed_mean = mean.windspeed;
        packet_data.cross_windspeed = sample.cross_wind; // Instantaneous, as shown by the client
        packet_data.cross_windspeed_mean = mean.cross_wind;
        packet_data.head_windspeed = mean.head_wind; // Mean, as shown by the client
        packet_data.baro_pressure = pressure;
        packet_data.maws_hour = hour;
        packet_data.maws_min = min;
        packet_data.maws_sec = sec;
        pthread_mutex_unlock(&lock_packetdata);
    }

    close_serial();
    return NULL;
}

/**
 * Packet timer, runs on the lws service thread.
 * Triggers sending of the packet data to all clients.
 */
static void packet_timer_cb(lws_sorted_usec_list_t *sul)
{
    lws_sul_schedule(context, 0, sul, packet_timer_cb, PACKET_INTERVAL_US);
    lws_callback_on_writable_all_protocol(context, &protocols[1]);
}

/**
 * Record timer, runs on the lws service thread.
 * Log data packet into file if record is active.
 */
static void record_timer_cb(lws_sorted_usec_list_t *sul)
{
    struct timespec ts;
    struct tm t;
    t_packet_data p;

    clock_gettime(CLOCK_REALTIME, &ts);
    // Fire just after the next full second so rows do not skip or repeat a second
    lws_sul_schedule(context, 0, sul, record_timer_cb, RECORD_INTERVAL_US - ts.tv_nsec / 1000 + 1000);

    if (record_fp == NULL)
        return;

    time_t now = ts.tv_sec;
    gmtime_r(&now, &t); // UTC, like the MAWS and GPS time

    pthread_mutex_lock(&lock_packetdata);
    p = packet_data;
    pthread_mutex_unlock(&lock_packetdata);

    if (p.record_status == 0)
        return;

    fprintf(record_fp, "%02u:%02u:%02u;%0.1f;%u;%0.1f;%u;%0.1f;%0.1f;%0.1f;%0.1f;%.0f;%02u:%02u:%02u;%u\n",
            t.tm_hour,
            t.tm_min,
            t.tm_sec,
            p.temperature,
            (unsigned)p.humidity,
            p.baro_pressure,
            (unsigned)p.wind_direction,
            fabs(p.windspeed),
            fabs(p.cross_windspeed),
            fabs(p.windspeed_mean),
            fabs(p.cross_windspeed_mean),
            p.gps_time,
            (unsigned)p.maws_hour,
            (unsigned)p.maws_min,
            (unsigned)p.maws_sec,
            (unsigned)p.top_number);
    fflush(record_fp);
    // Commit to flash every SYNC_EVERY rows: bounds data loss on power cut without wearing the SD card
    static unsigned rows;
    if (++rows % SYNC_EVERY == 0)
        fdatasync(fileno(record_fp));
}

/**
 * Well, it's main.
 */
int main(int argc, char **argv)
{
    int status = EXIT_SUCCESS;
    bool serial_started = false;
    bool gps_started = false;

    memset(&packet_data, 0, sizeof(packet_data));
    packet_data.runway_elevation = 1204; // Elevation[ft](Manching)
    packet_data.runway_heading = 248;    // Runway heading[deg](Manching)
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
    info.protocols = protocols;
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

    /* Create libwebsocket context representing this server */
    context = lws_create_context(&info);

    if (context == NULL)
    {
        lwsl_err("libwebsocket init failed\n");
        return EXIT_FAILURE;
    }

    /* Exit with failure if there is no weather station, so systemd restarts us. */
    if (open_serial() == EXIT_FAILURE)
    {
        lwsl_err("Serial device init failed\n");
        lws_context_destroy(context);
        return EXIT_FAILURE;
    }

    if (!gps_available)
        lwsl_notice("GPS disabled.\n");

    /* Reader threads must not take the termination signals, the main thread does. */
    sigset_t sigs, old_sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGQUIT);
    pthread_sigmask(SIG_BLOCK, &sigs, &old_sigs);

    /* Start reading serial data from weather station */
    if (pthread_create(&serial_thread, NULL, serial_read_thread, NULL) == 0)
    {
        serial_started = true;
    }
    else
    {
        lwsl_err("Cannot start serial thread\n");
        status = EXIT_FAILURE;
    }

    /* Initialize GPS data so we read back zero if no GPS is available */
    memset(&gpsdata, 0, sizeof(gpsdata));
    if (status == EXIT_SUCCESS && gps_available)
    {
        if (pthread_create(&gps_thread, NULL, gps_read_thread, NULL) == 0)
            gps_started = true;
        else
            lwsl_err("Cannot start GPS thread\n");
    }
    pthread_sigmask(SIG_SETMASK, &old_sigs, NULL);

    if (status == EXIT_SUCCESS)
    {
        packet_timer_cb(&packet_sul);
        record_timer_cb(&record_sul);

        // Run until SIGINT/SIGTERM (CTRL+C), the signal interrupts the wait
        while (!quit_requested)
        {
            lws_service(context, 100);
        }
    }

    lws_sul_cancel(&packet_sul);
    lws_sul_cancel(&record_sul);
    stop_recording();

    gps_thread_exit = true;
    serial_thread_exit = true;
    if (gps_started)
        pthread_join(gps_thread, NULL);
    if (serial_started)
        pthread_join(serial_thread, NULL);
    close_serial();

    lws_context_destroy(context);
    return status;
}
