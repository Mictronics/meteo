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
#include "help.h"
#include "serial.h"
#include "timer.h"
#include "meteoserver.h"
#define NOTUSED(V) ((void)V)
#define WSBUFFERSIZE 256 /* Byte */

static int debug_level = 0;
static int uid = -1, gid = -1, num_clients = 0;
static char interface_name[255] = "";
static const char *iface = NULL;
static int syslog_options = LOG_PID | LOG_PERROR;
static size_t packet_timer = 0;
t_packet_data packet_data;

#ifndef LWS_NO_DAEMONIZE
static int daemonize = 0;
#endif
static struct lws_context *context;
static struct lws_context_creation_info info;
static unsigned char wsbuffer[WSBUFFERSIZE];
static unsigned char *pwsbuffer = wsbuffer;
static int wsbuffer_len = 0;
pthread_mutex_t lock_established_conns;

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
const char *argp_program_version = "meteoserver v1.0";
const char args_doc[] = "";
const char doc[] = "Websocket Server for Vaisalla weather station\nLicense GPL-3+\n(C) 2020 Michael Wolf\n"
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

static unsigned int top_number = 0;
static unsigned int flight_number = 0;
static unsigned char record_status = 0;

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
 * Convert a double into byte buffer
 */
static size_t double_to_bytes(unsigned char *bytes_temp, double double_variable)
{
    memcpy(bytes_temp, (unsigned char *)(&double_variable), sizeof(double));
    return sizeof(double);
}

/**
 * Convert a uint64_t into byte buffer
 */
static size_t uint64_to_bytes(unsigned char *bytes_temp, uint64_t uint64_variable)
{
    memcpy(bytes_temp, (unsigned char *)(&uint64_variable), sizeof(uint64_t));
    return sizeof(uint64_t);
}

/**
 * Handle requests from client.
 */
static void handle_client_request(void *in, size_t len)
{
    unsigned char *id = (unsigned char *)in;

    switch (*id)
    {
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
    int n, m;

    switch (reason)
    {
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
        if (num_clients > 50)
        {
            lwsl_notice("50 clients already connected. New connection rejected...\n");
            return -1; /* Accept only one client on this server */
            // TODO Allow more than one client, implement master-slave handling
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
        if (!pss->publishing)
            break;
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
        lws_start_foreach_llp(struct per_session_data **,
                              ppss, vhd->pss_list)
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
 * Incoming signal handler. Close server nice and clean.
 */
static void sighandler(int sig)
{
    NOTUSED(sig);
    pthread_mutex_unlock(&lock_established_conns);
    pthread_mutex_destroy(&lock_established_conns);
    stop_timer(packet_timer);
    finalize_timer();
    close_serial();
    gps_close(&gpsdata);
    lws_cancel_service(context);
    lws_context_destroy(context);
    exit(EXIT_SUCCESS);
}

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
 * Callback function for timer 1.
 * Send packet data periodically to clients.
 */
static void timer1_handler(size_t timer_id, void *user_data)
{
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    pthread_mutex_lock(&lock_established_conns);
    packet_data.tm_hour = (unsigned char)timeinfo->tm_hour;
    packet_data.tm_min = (unsigned char)timeinfo->tm_min;
    packet_data.tm_sec = (unsigned char)timeinfo->tm_sec;
    packet_data.tm_mday = (unsigned char)timeinfo->tm_mday;
    packet_data.tm_mon = (unsigned char)timeinfo->tm_mon;
    packet_data.tm_year = (unsigned short)timeinfo->tm_year;
    packet_data.gps_status = (unsigned char)gpsdata.status;
    packet_data.gps_satellites_visible = (unsigned char)gpsdata.satellites_visible;
    packet_data.gps_satellites_used = (unsigned char)gpsdata.satellites_used;
    packet_data.gps_hdop = gpsdata.dop.hdop;
    packet_data.gps_pdop = gpsdata.dop.pdop;
    packet_data.gps_lat = gpsdata.fix.latitude;
    packet_data.gps_lon = gpsdata.fix.longitude;
    packet_data.gps_alt_msl = gpsdata.fix.altMSL;
    packet_data.gps_time = (long)gpsdata.fix.time.tv_sec;

    lws_callback_on_writable_all_protocol(context, &protocols[1]);
    pthread_mutex_unlock(&lock_established_conns);
}

/**
 * Well, it's main.
 */
int main(int argc, char **argv)
{
    memset(&packet_data, 0, sizeof(packet_data));

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
    info.ssl_cert_filepath = NULL;
    info.ssl_private_key_filepath = NULL;
    info.gid = -1;
    info.uid = -1;
    info.max_http_header_pool = 16;
    info.options = 0;
    info.extensions = NULL;
    info.timeout_secs = 5;
    info.ssl_cipher_list = NULL;
    info.max_http_header_data = 8192; /* Increased header length due to cookie size
                                       * Needs to be increased even more in case of
                                       * error "Ran out of header data space" during
                                       * client connect.
                                       */

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

    if (open_serial() == EXIT_FAILURE)
    {
        lwsl_err("Serial device init failed\n");
    };

    /* Initialize GPS data so we read back zero if no GPS is available */
    memset(&gpsdata, 0, sizeof(gpsdata));
    if (gps_available)
    {
        gpssource.server = (char *)"localhost";
        gpssource.port = (char *)DEFAULT_GPSD_PORT;
        gpssource.device = NULL;

        if (gps_open(gpssource.server, gpssource.port, &gpsdata) != 0)
        {
            gps_available = false;
            lwsl_err("No gpsd running or network error: %s\n", gps_errstr(errno));
        }

        unsigned int flags = WATCH_ENABLE;
        if (gpssource.device != NULL)
            flags |= WATCH_DEVICE;
        gps_stream(&gpsdata, flags, gpssource.device);
    }

    /* Create libwebsocket context representing this server */
    context = lws_create_context(&info);

    if (context == NULL)
    {
        lwsl_err("libwebsocket init failed\n");
        return EXIT_FAILURE;
    }

    initialize_timer();
    packet_timer = start_timer(1000, timer1_handler, TIMER_PERIODIC, NULL);

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
        if (gps_available)
        {
            if (gps_waiting(&gpsdata, 500000))
            {
                if (gps_read(&gpsdata, NULL, 0) == -1)
                {
                    lwsl_err("GPS read error.\n");
                }
            }
        }
    }

    sighandler(0);

    return EXIT_SUCCESS;
}
