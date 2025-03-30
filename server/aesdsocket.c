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
#include <pthread.h>
#include <time.h>

#define PORT "9000"
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define MAX_CONNECTIONS 10 // Define a maximum number of connections

int server_fd;
volatile sig_atomic_t running = 1; // Make running atomic
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct thread_data {
    int client_fd;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;
    pthread_t thread_id;
    struct thread_data* next;
} thread_data_t;

thread_data_t* thread_list = NULL;

void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    running = 0; // Set running to 0
    if (server_fd != -1) {
        close(server_fd);
    }

    // Use a temporary list to avoid modifying the original while iterating
    thread_data_t* current = thread_list;
    thread_data_t* next;
    while (current != NULL) {
        next = current->next; // Store the next pointer
        pthread_cancel(current->thread_id);
        pthread_join(current->thread_id, NULL);
        free(current);
        current = next;
    }
    thread_list = NULL; // Reset the list
    remove(DATA_FILE);
    closelog();
    pthread_mutex_destroy(&file_mutex);
    exit(0);
}

void* handle_connection(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    char client_ip[INET6_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    char *received_data = NULL;
    size_t received_data_len = 0;
    char *newline_pos;

    void *addr;
    if (data->client_addr.ss_family == AF_INET) {
        addr = &(((struct sockaddr_in *)&data->client_addr)->sin_addr);
    } else {
        addr = &(((struct sockaddr_in6 *)&data->client_addr)->sin6_addr);
    }
    inet_ntop(data->client_addr.ss_family, addr, client_ip, sizeof(client_ip));
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    while ((bytes_received = recv(data->client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        char *temp = realloc(received_data, received_data_len + bytes_received);
        if (temp == NULL) {
            syslog(LOG_ERR, "realloc error");
            break; // Exit the loop on error
        }
        received_data = temp;
        memcpy(received_data + received_data_len, buffer, bytes_received);
        received_data_len += bytes_received;

        while ((newline_pos = memchr(received_data, '\n', received_data_len)) != NULL) {
            size_t packet_len = newline_pos - received_data + 1;

            pthread_mutex_lock(&file_mutex);
            int data_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (data_fd == -1) {
                syslog(LOG_ERR, "Error opening/creating data file: %s", strerror(errno));
                pthread_mutex_unlock(&file_mutex);
                break; // Exit inner loop on error
            }
            if (write(data_fd, received_data, packet_len) == -1) {
                syslog(LOG_ERR, "Error writing to data file: %s", strerror(errno));
                close(data_fd);
                pthread_mutex_unlock(&file_mutex);
                break; // Exit inner loop on error
            }
            close(data_fd);
            pthread_mutex_unlock(&file_mutex);

            pthread_mutex_lock(&file_mutex);
            int read_fd = open(DATA_FILE, O_RDONLY);
            if (read_fd == -1) {
                syslog(LOG_ERR, "Error opening file for reading: %s", strerror(errno));
                pthread_mutex_unlock(&file_mutex);
                break; // Exit inner loop
            }
            if (lseek(read_fd, 0, SEEK_SET) == -1) {
                syslog(LOG_ERR, "lseek error: %s", strerror(errno));
                close(read_fd);
                pthread_mutex_unlock(&file_mutex);
                break;
            }

            char *file_content = NULL;
            size_t file_size = 0;
            ssize_t read_size;
            while ((read_size = read(read_fd, buffer, BUFFER_SIZE)) > 0) {
                char *temp_file = realloc(file_content, file_size + read_size);
                if (temp_file == NULL) {
                    syslog(LOG_ERR, "realloc error");
                    break; // Exit inner loop
                }
                file_content = temp_file;
                memcpy(file_content + file_size, buffer, read_size);
                file_size += read_size;
            }
            if (read_size == -1) {
                syslog(LOG_ERR, "Error reading from file: %s", strerror(errno));
            }

            if (file_content != NULL) {
                if (send(data->client_fd, file_content, file_size, 0) == -1)
                {
                    syslog(LOG_ERR, "Error sending data to client. %s", strerror(errno));
                    free(file_content);
                    close(read_fd);
                    pthread_mutex_unlock(&file_mutex);
                    break; // Exit Inner Loop
                }
                free(file_content);
            }
            close(read_fd);
            pthread_mutex_unlock(&file_mutex);

            received_data_len -= packet_len;
            memmove(received_data, received_data + packet_len, received_data_len);
        }
    }

    free(received_data);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(data->client_fd);

    pthread_mutex_lock(&file_mutex);
    thread_data_t* current = thread_list;
    thread_data_t* prev = NULL;
    while (current != NULL) {
        if (current == data) {
            if (prev == NULL) {
                thread_list = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }
    pthread_mutex_unlock(&file_mutex);
    return NULL;
}

void* write_timestamp(void* arg) {
    while (running) {
        sleep(10);
        time_t now;
        struct tm *tm_info;
        char timestamp[64];

        time(&now);
        tm_info = localtime(&now);
        strftime(timestamp, sizeof(timestamp), "timestamp:%Y,%m,%d,%H,%M,%S\n", tm_info);

        pthread_mutex_lock(&file_mutex);
        int data_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (data_fd == -1) {
            syslog(LOG_ERR, "Error opening/creating data file for timestamp: %s", strerror(errno));
        } else {
            if (write(data_fd, timestamp, strlen(timestamp)) == -1) {
                syslog(LOG_ERR, "Error writing timestamp to data file: %s", strerror(errno));
            }
            close(data_fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    struct addrinfo hints, *res, *p;
    int daemon_mode = 0;
    pthread_t timestamp_thread;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    openlog(NULL, 0, LOG_USER);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

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

        // close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        //redirect standard descriptors to /dev/null
        int fd = open("/dev/null", O_RDWR);
        if (fd != -1) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    }

    if (listen(server_fd, MAX_CONNECTIONS) == -1) {
        syslog(LOG_ERR, "Error listening for connections: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (pthread_create(&timestamp_thread, NULL, write_timestamp, NULL) != 0) {
        syslog(LOG_ERR, "pthread_create error for timestamp thread");
        close(server_fd);
        return -1;
    }

    while (running) {
        thread_data_t* data = malloc(sizeof(thread_data_t));
        if (data == NULL) {
            syslog(LOG_ERR, "malloc error");
            continue; // Continue listening for other connections
        }

        data->client_addr_len = sizeof(data->client_addr);
        data->client_fd = accept(server_fd, (struct sockaddr *)&data->client_addr, &data->client_addr_len);
        if (data->client_fd == -1) {
            if (running) syslog(LOG_ERR, "Error accepting connection: %s", strerror(errno));
            free(data);
            continue; // Continue listening
        }

        if (pthread_create(&data->thread_id, NULL, handle_connection, data) != 0) {
            syslog(LOG_ERR, "pthread_create error");
            close(data->client_fd);
            free(data);
            continue; // Continue listening
        }

        pthread_mutex_lock(&file_mutex);
        data->next = thread_list;
        thread_list = data;
        pthread_mutex_unlock(&file_mutex);
    }

    pthread_join(timestamp_thread, NULL);
    // Code will reach here after signal is caught
    return 0;
}
