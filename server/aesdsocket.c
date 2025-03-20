#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <sys/stat.h>

#define PORT "9000"
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

int server_fd;
int running = 1;

void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    running = 0;
    if (server_fd != -1) {
        close(server_fd);
    }
    remove(DATA_FILE);
    closelog();
    exit(0);
}

int main(int argc, char *argv[]) {
    struct addrinfo hints, *res, *p;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char client_ip[INET6_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];
    int data_fd, client_fd;
    ssize_t bytes_received;
    char *received_data = NULL;
    size_t received_data_len = 0;
    char *newline_pos;
    int daemon_mode = 0;

    openlog(NULL, 0, LOG_USER);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        syslog(LOG_ERR, "getaddrinfo error: %s", gai_strerror(errno));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_fd == -1) {
            continue;
        }

        int reuse = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
            syslog(LOG_ERR, "setsockopt error: %s", strerror(errno));
            close(server_fd);
            continue;
        }

        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(server_fd);
    }

    if (p == NULL) {
        syslog(LOG_ERR, "Could not bind to any address");
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    if (listen(server_fd, 1) == -1) {
        syslog(LOG_ERR, "Error listening for connections: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "fork error");
            close(server_fd);
            return -1;
        }
        if (pid > 0) {
            exit(0); // Parent exits
        }
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid error");
            close(server_fd);
            return -1;
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    while (running) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (running) syslog(LOG_ERR, "Error accepting connection: %s", strerror(errno));
            continue;
        }

        void *addr;
        if (client_addr.ss_family == AF_INET) {
            addr = &(((struct sockaddr_in *)&client_addr)->sin_addr);
        } else {
            addr = &(((struct sockaddr_in6 *)&client_addr)->sin6_addr);
        }
        inet_ntop(client_addr.ss_family, addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        data_fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
        if (data_fd == -1) {
            syslog(LOG_ERR, "Error opening/creating data file: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        received_data_len = 0;
        free(received_data);
        received_data = NULL;

        while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            char *temp = realloc(received_data, received_data_len + bytes_received);
            if (temp == NULL) {
                syslog(LOG_ERR, "realloc error");
                break;
            }
            received_data = temp;
            memcpy(received_data + received_data_len, buffer, bytes_received);
            received_data_len += bytes_received;

            while ((newline_pos = memchr(received_data, '\n', received_data_len)) != NULL) {
                size_t packet_len = newline_pos - received_data + 1;
                if (write(data_fd, received_data, packet_len) == -1) {
                    syslog(LOG_ERR, "Error writing to data file: %s", strerror(errno));
                }

                lseek(data_fd, 0, SEEK_SET);

                char *file_content = NULL;
                size_t file_size = 0;
                ssize_t read_size;
                int read_fd = open(DATA_FILE, O_RDONLY);
                if(read_fd == -1){
                    syslog(LOG_ERR, "Error opening file for reading: %s", strerror(errno));
                    break;
                }

                while ((read_size = read(read_fd, buffer, BUFFER_SIZE)) > 0) {
                    char *temp_file = realloc(file_content, file_size + read_size);
                    if(temp_file == NULL){
                        syslog(LOG_ERR, "realloc error");
                        break;
                    }
                    file_content = temp_file;
                    memcpy(file_content + file_size, buffer, read_size);
                    file_size += read_size;
                }
                if(read_size == -1){
                    syslog(LOG_ERR, "Error reading from file: %s", strerror(errno));
                }

                if(file_content != NULL){
                    send(client_fd, file_content, file_size, 0);
                    free(file_content);
                }

                close(read_fd);

                received_data_len -= packet_len;
                memmove(received_data, received_data + packet_len, received_data_len);
            }
        }
        free(received_data);
        received_data = NULL;

        close(data_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
    }
    return 0;
}