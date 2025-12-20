// client.c
#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 256

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static void write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            die("write");
        }
        off += (size_t)n;
    }
}

static int connect_tcp(const char *addr, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // IPv4/IPv6
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(addr, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        exit(1);
    }

    int sock = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        fprintf(stderr, "failed to connect\n");
        exit(1);
    }
    return sock;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s addr port name\n", argv[0]);
        return 1;
    }

    const char *addr = argv[1];
    const char *port = argv[2];
    const char *name = argv[3];

    int sock = connect_tcp(addr, port);

    // Send name immediately: "<name>\n"
    char name_line[MAX_LINE];
    snprintf(name_line, sizeof(name_line), "%s\n", name);
    write_all(sock, name_line, strlen(name_line));

    // Event loop: watch stdin + socket
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sock, &rfds);

        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            die("select");
        }

        // 1) Incoming from server
        if (FD_ISSET(sock, &rfds)) {
            char buf[1024];
            ssize_t n = read(sock, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) continue;
                die("read(sock)");
            }
            if (n == 0) {
                // server closed connection
                close(sock);
                return 0;
            }
            // Print exactly what we got
            write_all(STDOUT_FILENO, buf, (size_t)n);
        }

        // 2) User typed something
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char line[MAX_LINE];

            if (!fgets(line, sizeof(line), stdin)) {
                // EOF on stdin
                // choose to exit gracefully
                close(sock);
                return 0;
            }

            // Send line to server as-is
            write_all(sock, line, strlen(line));

            // If user requested exit: "!exit\n" (or "!exit" without newline)
            // We'll handle both safely.
            char tmp[MAX_LINE];
            strncpy(tmp, line, sizeof(tmp));
            tmp[sizeof(tmp) - 1] = '\0';
            size_t L = strlen(tmp);
            if (L > 0 && tmp[L - 1] == '\n') tmp[L - 1] = '\0';

            if (strcmp(tmp, "!exit") == 0) {
                printf("client exiting\n");
                close(sock);
                return 0;
            }
        }
    }
}
