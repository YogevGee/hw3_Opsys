#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#define MAX_CLIENTS 16
#define BUFFER_SIZE 512  

// Structure to track a connected client
typedef struct {
    int fd;                     // Socket file descriptor
    char name[50];              // Client name (empty if not yet set)
    char ip[INET_ADDRSTRLEN];   // Client IP address
    
    // Per-client buffer to handle TCP stream fragmentation/fusion
    char pending_buf[BUFFER_SIZE]; 
    int pending_len;
} client_t;

client_t clients[MAX_CLIENTS];

// Initialize clients array
void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        memset(clients[i].name, 0, sizeof(clients[i].name));
        memset(clients[i].ip, 0, sizeof(clients[i].ip));
        memset(clients[i].pending_buf, 0, sizeof(clients[i].pending_buf));
        clients[i].pending_len = 0;
    }
}

// Add a new client (initially without a name)
int add_client(int fd, char *ip) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1) {
            clients[i].fd = fd;
            // Name is left empty to indicate "Waiting for Handshake"
            memset(clients[i].name, 0, sizeof(clients[i].name));
            
            strncpy(clients[i].ip, ip, sizeof(clients[i].ip) - 1);
            clients[i].pending_len = 0;
            memset(clients[i].pending_buf, 0, sizeof(clients[i].pending_buf));
            return 0;
        }
    }
    return -1;
}

void remove_client(int i) {
    if (i < 0 || i >= MAX_CLIENTS) return;
    if (clients[i].fd != -1) {
        close(clients[i].fd);
        clients[i].fd = -1;
        clients[i].pending_len = 0;
        // Don't need to zero out name/ip strictly
    }
}

int get_client_index_by_name(char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1 && strcmp(clients[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Helper to send data to a specific fd
void send_to_fd(int fd, const char *buf, size_t len) {
    size_t total = 0;
    size_t bytesleft = len;
    ssize_t n;
    while (total < len) {
        n = send(fd, buf + total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }
}

// Helper to broadcast to ALL clients (including sender)
void broadcast_msg(const char *msg) {
    size_t len = strlen(msg);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1 && strlen(clients[i].name) > 0) {
            send_to_fd(clients[i].fd, msg, len);
        }
    }
}

// Process a single complete line of text from a client
void process_packet(int client_idx, char *line) {
    // Trim 
    line[strcspn(line, "\r\n")] = 0;
    if (strlen(line) == 0) return;

    //STATE 1: Handshake (Client has no name yet)
    if (strlen(clients[client_idx].name) == 0) {
        // The first line received is the name
        strncpy(clients[client_idx].name, line, sizeof(clients[client_idx].name) - 1);
        clients[client_idx].name[sizeof(clients[client_idx].name) - 1] = '\0';
        
        printf("client %s connected from %s\n", clients[client_idx].name, clients[client_idx].ip);
        return;
    }

    // STATE 2: Chat Mode
    
    // Format: "sourcename: original_message\n"
    char formatted_msg[BUFFER_SIZE + 64]; 
    snprintf(formatted_msg, sizeof(formatted_msg), "%s: %s\n", clients[client_idx].name, line);

    // Check for Whisper: "@friend msg"
    if (line[0] == '@') {
        char *space_ptr = strchr(line, ' ');
        if (space_ptr) {
            char target_name[50];
            int name_len = space_ptr - (line + 1);
            if (name_len >= (int)sizeof(target_name)) name_len = sizeof(target_name) - 1;

            strncpy(target_name, line + 1, name_len);
            target_name[name_len] = '\0';

            int target_idx = get_client_index_by_name(target_name);
            if (target_idx != -1) {
                // Send to target
                send_to_fd(clients[target_idx].fd, formatted_msg, strlen(formatted_msg));

            }
        } else {
            // Malformed whisper (no space), treat as normal or ignore. 
            // We treat as normal broadcast based on typical strict parsing, or ignore.
            // Let's broadcast it as a normal message if parsing fails.
             broadcast_msg(formatted_msg);
        }
    } else {
        // Normal Broadcast
        broadcast_msg(formatted_msg);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    int listener_fd, new_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    fd_set read_fds, temp_fds; 
    int fd_max;

    init_clients();

    // Setup Socket
    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) { perror("socket"); exit(1); }

    int yes = 1;
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt"); exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listener_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(listener_fd, 10) < 0) { perror("listen"); exit(1); }

    FD_ZERO(&read_fds);
    FD_SET(listener_fd, &read_fds);
    fd_max = listener_fd;

    while (1) {
        temp_fds = read_fds;
        if (select(fd_max + 1, &temp_fds, NULL, NULL, NULL) == -1) {
            perror("select"); exit(1);
        }

        for (int i = 0; i <= fd_max; i++) {
            if (FD_ISSET(i, &temp_fds)) {
                
                //New Connection 
                if (i == listener_fd) {
                    addr_len = sizeof(client_addr);
                    new_fd = accept(listener_fd, (struct sockaddr*)&client_addr, &addr_len);
                    if (new_fd == -1) {
                        perror("accept");
                    } else {
                        // NON-BLOCKING: Just add to list, don't recv name yet
                        if (add_client(new_fd, inet_ntoa(client_addr.sin_addr)) == 0) {
                            FD_SET(new_fd, &read_fds);
                            if (new_fd > fd_max) fd_max = new_fd;
                        } else {
                            close(new_fd);
                        }
                    }
                } 
                // Data from Client
                else {
                    // Find which client struct corresponds to this fd
                    int client_idx = -1;
                    for(int c=0; c<MAX_CLIENTS; c++){
                        if(clients[c].fd == i) { client_idx = c; break; }
                    }

                    if (client_idx == -1) {
                        // Should not happen, but clean up if it does
                        close(i); FD_CLR(i, &read_fds); continue;
                    }

                    char temp_buf[256];
                    ssize_t nbytes = recv(i, temp_buf, sizeof(temp_buf), 0);

                    if (nbytes <= 0) {
                        // Disconnected
                        if (strlen(clients[client_idx].name) > 0) {
                             printf("client %s disconnected\n", clients[client_idx].name);
                        }
                        remove_client(client_idx); // closes fd
                        FD_CLR(i, &read_fds);
                    } else {
                        // Append to pending buffer (Safe Buffer Handling)
                        int space_left = sizeof(clients[client_idx].pending_buf) - clients[client_idx].pending_len - 1;
                        if (nbytes > space_left) nbytes = space_left; // Prevent overflow
                        
                        memcpy(clients[client_idx].pending_buf + clients[client_idx].pending_len, temp_buf, nbytes);
                        clients[client_idx].pending_len += nbytes;
                        clients[client_idx].pending_buf[clients[client_idx].pending_len] = '\0';

                        // Process ALL newlines in buffer
                        char *newline_ptr;
                        while ((newline_ptr = strchr(clients[client_idx].pending_buf, '\n')) != NULL) {
                            *newline_ptr = '\0'; // Null-terminate the line
                            
                            // Process the extracted line
                            process_packet(client_idx, clients[client_idx].pending_buf);

                            // Shift remaining data to start of buffer
                            int line_len = (newline_ptr - clients[client_idx].pending_buf) + 1; // +1 for \n
                            int remaining = clients[client_idx].pending_len - line_len;
                            
                            memmove(clients[client_idx].pending_buf, newline_ptr + 1, remaining);
                            clients[client_idx].pending_len = remaining;
                            clients[client_idx].pending_buf[remaining] = '\0';
                        }
                    }
                }
            }
        }
    }
    return 0;
}