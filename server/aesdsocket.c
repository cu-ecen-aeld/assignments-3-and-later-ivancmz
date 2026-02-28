#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT        "9000"
#define DATA_FILE   "/var/tmp/aesdsocketdata"
#define BUF_SIZE    1024

static volatile sig_atomic_t g_caught_signal = 0;
static int g_sockfd = -1;

/* Exit gracefully on SIGINT or SIGTERM */
static void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        g_caught_signal = 1;
        /* Unblock accept() by shutting down the listening socket */
        if (g_sockfd != -1) {
            shutdown(g_sockfd, SHUT_RDWR);
        }
    }
}

/* Call the handler when signals are received */
static int setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction SIGTERM");
        return -1;
    }
    return 0;
}

/**
 * Send the entire contents of DATA_FILE to the client socket.
 * Returns 0 on success, -1 on error.
 */
static int send_file_to_client(int clientfd)
{
    FILE *fp = fopen(DATA_FILE, "r");
    if (!fp) {
        syslog(LOG_ERR, "Failed to open %s for reading: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    char buf[BUF_SIZE];
    ssize_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        ssize_t total_sent = 0;
        while (total_sent < nread) {
            ssize_t sent = send(clientfd, buf + total_sent, nread - total_sent, 0);
            if (sent == -1) {
                syslog(LOG_ERR, "send error: %s", strerror(errno));
                fclose(fp);
                return -1;
            }
            total_sent += sent;
        }
    }

    fclose(fp);
    return 0;
}

/**
 * Handle a single client connection using stdio buffering.
 * fdopen wraps the socket fd so fgets handles internal buffering,
 * making it easier to read lines of input. I still use send() for output
 */
static void handle_connection(int clientfd)
{
    FILE *client_stream = fdopen(clientfd, "r");
    if (!client_stream) {
        syslog(LOG_ERR, "fdopen: %s", strerror(errno));
        close(clientfd);
        return;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t nread;

    while (!g_caught_signal && (nread = getline(&line, &line_cap, client_stream)) > 0) {
        /* getline reads until \n (inclusive) or EOF */
        if (line[nread - 1] != '\n') {
            /* Incomplete packet (EOF without newline), discard i guess */
            break;
        }

        /* Append packet to data file */
        FILE *fp = fopen(DATA_FILE, "a");
        if (!fp) {
            syslog(LOG_ERR, "Failed to open %s for appending: %s", DATA_FILE, strerror(errno));
            break;
        }
        fwrite(line, 1, nread, fp);
        fclose(fp);

        /* Send full file contents back */
        send_file_to_client(clientfd);
    }

    free(line);
    /* fclose would close the underlying fd; we let the caller close it */
    fclose(client_stream);
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
        case 'd':
            daemon_mode = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            return -1;
        }
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    if (setup_signals() == -1) {
        return -1;
    }

    /* Resolve address for binding */
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

    /* Daemonize after successful bind */
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "fork: %s", strerror(errno));
            close(g_sockfd);
            return -1;
        }
        if (pid > 0) {
            /* Parent exits successfully */
            return 0;
        }
        /* Child continues as daemon */
        setsid();
        chdir("/");

        /* Redirect stdin/stdout/stderr to /dev/null */
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

    /* Accept loop */
    while (!g_caught_signal) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int clientfd = accept(g_sockfd, (struct sockaddr *)&client_addr, &addr_len);
        if (clientfd == -1) {
            if (g_caught_signal) {
                break;
            }
            syslog(LOG_ERR, "accept: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_connection(clientfd);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        /* clientfd already closed by fclose() inside handle_connection */
    }

    /* Cleanup */
    close(g_sockfd);
    remove(DATA_FILE);
    closelog();

    return 0;
}
