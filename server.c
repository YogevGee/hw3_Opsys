#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_CLIENTS 16
#define BUFFER_SIZE 256

// Structure to track a connected client
typedef struct {
    int fd;                     // Socket file descriptor
    char name[50];              // Client name
    char ip[INET_ADDRSTRLEN];   // Client IP address
} client_t;

// Global array to manage clients
client_t clients[MAX_CLIENTS];


// Initialize the clients array (FIX 2.1: Safer initialization)
void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1; // -1 indicates empty slot
        memset(clients[i].name, 0, sizeof(clients[i].name));
        memset(clients[i].ip, 0, sizeof(clients[i].ip));
    }
}

// Add a new client to the array 
// Returns 0 on success, -1 if server is full
int add_client(int fd, char *name, char *ip) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1) {
            clients[i].fd = fd;

            strncpy(clients[i].name, name, sizeof(clients[i].name) - 1);
            clients[i].name[sizeof(clients[i].name) - 1] = '\0';

            strncpy(clients[i].ip, ip, sizeof(clients[i].ip) - 1);
            clients[i].ip[sizeof(clients[i].ip) - 1] = '\0';

            return 0; // Success
        }
    }
    return -1; // Server full
}

// Remove a client from the array by File Descriptor
void remove_client(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == fd) {
            clients[i].fd = -1; 
            memset(clients[i].name, 0, sizeof(clients[i].name));
            memset(clients[i].ip, 0, sizeof(clients[i].ip));
            return;
        }
    }
}

// Find a client's index by their name (returns -1 if not found)
int get_client_index_by_name(char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1 && strcmp(clients[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Find a client's index by their File Descriptor
int get_client_index_by_fd(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

ssize_t send_all(int fd, const char *buf, size_t len) {
    size_t total = 0;
    size_t bytesleft = len;
    ssize_t n;

    while (total < len) {
        n = send(fd, buf + total, bytesleft, 0);
        if (n == -1) { break; } // Error
        if (n == 0) { break; }  // Disconnected
        total += n;
        bytesleft -= n;
    }
    return (n == -1) ? -1 : total; 
}

int main(int argc, char *argv[]) {
    // Check arguments
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

    // 1. Socket Setup
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

    // Main Loop
    while (1) {
        temp_fds = read_fds; // Backup because select() modifies the set

        if (select(fd_max + 1, &temp_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(1);
        }

        for (int i = 0; i <= fd_max; i++) {
            if (FD_ISSET(i, &temp_fds)) {
                
                // CASE 1: New Connection on Listener Socket
                if (i == listener_fd) {
                    addr_len = sizeof(client_addr);
                    new_fd = accept(listener_fd, (struct sockaddr*)&client_addr, &addr_len);
                    
                    if (new_fd == -1) { 
                        perror("accept"); 
                    } else {
                        // Handshake: Get name immediately
                        char name_buf[50];
                        memset(name_buf, 0, sizeof(name_buf));
                        
                        int bytes = recv(new_fd, name_buf, sizeof(name_buf) - 1, 0);
                        
                        if (bytes > 0) {
                            name_buf[strcspn(name_buf, "\n")] = 0;
                            name_buf[strcspn(name_buf, "\r")] = 0;
                            if (add_client(new_fd, name_buf, inet_ntoa(client_addr.sin_addr)) == 0) {
                                FD_SET(new_fd, &read_fds);
                                if (new_fd > fd_max) { fd_max = new_fd; }
                                printf("client %s connected from %s\n", name_buf, inet_ntoa(client_addr.sin_addr));
                            } else {
                                // Server full
                                close(new_fd);
                            }
                        } else {
                            close(new_fd); 
                        }
                    }
                } 
                // CASE 2: Data from Existing Client
                else {
                    char buf[BUFFER_SIZE];
                    memset(buf, 0, BUFFER_SIZE);
                    int nbytes = recv(i, buf, sizeof(buf) - 1, 0);

                    int sender_idx = get_client_index_by_fd(i);
                    // Safety check
                    if (sender_idx == -1) {
                         close(i); FD_CLR(i, &read_fds); continue;
                    }

                    if (nbytes <= 0) {
                        // Disconnection
                        printf("client %s disconnected\n", clients[sender_idx].name);
                        close(i);
                        FD_CLR(i, &read_fds);
                        remove_client(i);
                    } 
                    else {
                        buf[strcspn(buf, "\n")] = 0;
                        buf[strcspn(buf, "\r")] = 0;
                        if (strlen(buf) == 0) continue; 

                        
                        char msg_to_send[BUFFER_SIZE + 64]; 
                        snprintf(msg_to_send, sizeof(msg_to_send), "%s: %s\n", clients[sender_idx].name, buf);

                        // Whisper Check
                        if (buf[0] == '@') {
                            char *space_ptr = strchr(buf, ' ');
                            if (space_ptr != NULL) {
                                char friend_name[50];
                                int name_len = space_ptr - (buf + 1);
                                if(name_len > 49) name_len = 49;
                                
                                strncpy(friend_name, buf + 1, name_len);
                                friend_name[name_len] = '\0';

                                int target_idx = get_client_index_by_name(friend_name);
                                
                                if (target_idx != -1) {
                                    send_all(clients[target_idx].fd, msg_to_send, strlen(msg_to_send));
                                } 
                            }
                        } 
                        else {
                            // Broadcast
                            for (int j = 0; j < MAX_CLIENTS; j++) {
                                if (clients[j].fd != -1) {
                                    send_all(clients[j].fd, msg_to_send, strlen(msg_to_send));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}