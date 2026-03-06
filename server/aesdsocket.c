#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/queue.h>

/* glibc's sys/queue.h omits SLIST_FOREACH_SAFE; provide it here */
#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar)          \
    for ((var) = SLIST_FIRST(head);                         \
         (var) && ((tvar) = SLIST_NEXT(var, field), 1);     \
         (var) = (tvar))
#endif

#define PORT                    "9000"
#define DATA_FILE               "/var/tmp/aesdsocketdata"
#define BUF_SIZE                1024
#define TIMESTAMP_INTERVAL_SECS 10

static volatile sig_atomic_t g_caught_signal = 0;
static int                   g_sockfd        = -1;

/* Protects all writes (and send-backs) to DATA_FILE */
static pthread_mutex_t g_file_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Thread list ── */
struct thread_entry {
    pthread_t            tid;
    int                  clientfd;
    volatile int         done;
    SLIST_ENTRY(thread_entry) entries;
};
SLIST_HEAD(thread_list_head, thread_entry) g_thread_list;

/* ── Signal handling ── */
static void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        g_caught_signal = 1;
        if (g_sockfd != -1)
            shutdown(g_sockfd, SHUT_RDWR);
    }
}

static int setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT,  &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    /* Ignore SIGPIPE so a broken connection doesn't kill the process */
    signal(SIGPIPE, SIG_IGN);
    return 0;
}

/* ── File helpers ── */

/* Append one complete packet to DATA_FILE.  Caller must hold g_file_mutex. */
static int append_to_file(const char *data, size_t len)
{
    FILE *fp = fopen(DATA_FILE, "a");
    if (!fp) {
        syslog(LOG_ERR, "fopen append %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }
    fwrite(data, 1, len, fp);
    fclose(fp);
    return 0;
}

/* Send entire DATA_FILE to clientfd.  Caller must hold g_file_mutex. */
static int send_file_to_client(int clientfd)
{
    FILE *fp = fopen(DATA_FILE, "r");
    if (!fp) {
        syslog(LOG_ERR, "fopen read %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    char   buf[BUF_SIZE];
    size_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        size_t total_sent = 0;
        while (total_sent < nread) {
            ssize_t sent = send(clientfd, buf + total_sent, nread - total_sent, 0);
            if (sent == -1) {
                syslog(LOG_ERR, "send: %s", strerror(errno));
                fclose(fp);
                return -1;
            }
            total_sent += (size_t)sent;
        }
    }

    fclose(fp);
    return 0;
}

/* ── Worker thread ── */

struct conn_args {
    int                  clientfd;
    char                 client_ip[INET_ADDRSTRLEN];
    struct thread_entry *entry;
};

static void *connection_thread(void *arg)
{
    struct conn_args *ca = (struct conn_args *)arg;
    int   clientfd = ca->clientfd;
    char  client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, ca->client_ip, sizeof(client_ip) - 1);
    client_ip[sizeof(client_ip) - 1] = '\0';
    struct thread_entry *entry = ca->entry;
    free(ca);

    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    FILE *stream = fdopen(clientfd, "r");
    if (!stream) {
        syslog(LOG_ERR, "fdopen: %s", strerror(errno));
        close(clientfd);
        entry->done = 1;
        return NULL;
    }

    char   *line     = NULL;
    size_t  line_cap = 0;
    ssize_t nread;

    while (!g_caught_signal && (nread = getline(&line, &line_cap, stream)) > 0) {
        if (line[nread - 1] != '\n')   /* incomplete packet, drop */
            break;

        pthread_mutex_lock(&g_file_mutex);
        if (append_to_file(line, (size_t)nread) == 0)
            send_file_to_client(clientfd);
        pthread_mutex_unlock(&g_file_mutex);
    }

    free(line);
    fclose(stream);   /* also closes clientfd */

    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    entry->done = 1;
    return NULL;
}

/* ── Timer thread ── */

static void *timer_thread(void *arg)
{
    (void)arg;
    struct timespec sleep_ts = { TIMESTAMP_INTERVAL_SECS, 0 };

    while (!g_caught_signal) {
        struct timespec rem;
        if (nanosleep(&sleep_ts, &rem) == -1 && errno == EINTR) {
            if (g_caught_signal)
                break;
            sleep_ts = rem;   /* resume remaining interval */
            continue;
        }
        sleep_ts.tv_sec  = TIMESTAMP_INTERVAL_SECS;
        sleep_ts.tv_nsec = 0;

        if (g_caught_signal)
            break;

        time_t     now   = time(NULL);
        struct tm *tm_p  = localtime(&now);
        char       tbuf[128];
        strftime(tbuf, sizeof(tbuf), "%a, %d %b %Y %H:%M:%S %z", tm_p);

        pthread_mutex_lock(&g_file_mutex);
        FILE *fp = fopen(DATA_FILE, "a");
        if (fp) {
            fprintf(fp, "timestamp:%s\n", tbuf);
            fclose(fp);
        } else {
            syslog(LOG_ERR, "timer fopen: %s", strerror(errno));
        }
        pthread_mutex_unlock(&g_file_mutex);
    }

    return NULL;
}

/* ── Thread reaping ── */

/* Join and free any thread entries whose done flag is set. */
static void reap_done_threads(void)
{
    struct thread_entry *e, *tmp;
    SLIST_FOREACH_SAFE(e, &g_thread_list, entries, tmp) {
        if (e->done) {
            pthread_join(e->tid, NULL);
            SLIST_REMOVE(&g_thread_list, e, thread_entry, entries);
            free(e);
        }
    }
}

/* ── main ── */

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
        case 'd': daemon_mode = 1; break;
        default:
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            return -1;
        }
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    if (setup_signals() == -1)
        return -1;

    SLIST_INIT(&g_thread_list);

    /* Resolve bind address */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    int rc = getaddrinfo(NULL, PORT, &hints, &res);
    if (rc != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(rc));
        return -1;
    }

    g_sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (g_sockfd == -1) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }

    int optval = 1;
    if (setsockopt(g_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        syslog(LOG_ERR, "setsockopt: %s", strerror(errno));
        close(g_sockfd);
        freeaddrinfo(res);
        return -1;
    }

    if (bind(g_sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        close(g_sockfd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    /* Daemonize after successful bind, before spawning threads */
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "fork: %s", strerror(errno));
            close(g_sockfd);
            return -1;
        }
        if (pid > 0)
            return 0;   /* parent exits */
        setsid();
        chdir("/");
        int devnull = open("/dev/null", O_RDWR);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO)
                close(devnull);
        }
    }

    if (listen(g_sockfd, 10) == -1) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        close(g_sockfd);
        return -1;
    }

    /* Start timestamp timer thread (in child after daemonize) */
    pthread_t tid_timer;
    if (pthread_create(&tid_timer, NULL, timer_thread, NULL) != 0) {
        syslog(LOG_ERR, "pthread_create timer: %s", strerror(errno));
        close(g_sockfd);
        return -1;
    }

    /* Accept loop */
    while (!g_caught_signal) {
        reap_done_threads();

        struct sockaddr_in client_addr;
        socklen_t          addr_len = sizeof(client_addr);
        int clientfd = accept(g_sockfd, (struct sockaddr *)&client_addr, &addr_len);
        if (clientfd == -1) {
            if (g_caught_signal)
                break;
            syslog(LOG_ERR, "accept: %s", strerror(errno));
            continue;
        }

        struct thread_entry *entry = malloc(sizeof(*entry));
        struct conn_args    *ca    = malloc(sizeof(*ca));
        if (!entry || !ca) {
            syslog(LOG_ERR, "malloc: %s", strerror(errno));
            close(clientfd);
            free(entry);
            free(ca);
            continue;
        }
        entry->clientfd = clientfd;
        entry->done     = 0;
        ca->clientfd    = clientfd;
        ca->entry       = entry;
        inet_ntop(AF_INET, &client_addr.sin_addr, ca->client_ip, sizeof(ca->client_ip));

        if (pthread_create(&entry->tid, NULL, connection_thread, ca) != 0) {
            syslog(LOG_ERR, "pthread_create: %s", strerror(errno));
            close(clientfd);
            free(entry);
            free(ca);
            continue;
        }
        SLIST_INSERT_HEAD(&g_thread_list, entry, entries);
    }

    /* Unblock any threads blocked in recv/getline */
    struct thread_entry *e;
    SLIST_FOREACH(e, &g_thread_list, entries)
        shutdown(e->clientfd, SHUT_RDWR);

    /* Join all remaining worker threads */
    struct thread_entry *tmp;
    SLIST_FOREACH_SAFE(e, &g_thread_list, entries, tmp) {
        pthread_join(e->tid, NULL);
        SLIST_REMOVE(&g_thread_list, e, thread_entry, entries);
        free(e);
    }

    /* Interrupt timer thread's nanosleep, then join it */
    pthread_kill(tid_timer, SIGTERM);
    pthread_join(tid_timer, NULL);

    close(g_sockfd);
    remove(DATA_FILE);
    closelog();

    return 0;
}
