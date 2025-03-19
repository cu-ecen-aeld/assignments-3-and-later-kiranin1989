
#define PORT_ID "9000"
#define BUFFER_SIZE 1024

int main(void)
{
    int server_fd,client_fd;
    struct sockaddr client_addr;
    struct addrinfo hints, *servinfo;
    char buffer[BUFFER_SIZE];
    socklen_t client_addr_len = sizeof(client_addr);
    rxdata = NULL;
    rxdata_len = 0;
        
    openlog(NULL,0,LOG_USER);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // Aeither IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT_ID, &hints, &servinfo) != 0) {
        perror("socket");
        syslog(LOG_ERR, "getaddrinfo error");
        return -1;
    }

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        syslog(LOG_ERR,"failed to create socket file");
        return -1;
    }

    //bind socket
    if (bind(server_fd, servinfo->ai_addr, servinfo->ai_addrlen) != 0)
    {
        error("socket");
        syslog(LOG_ERR,"failed to bind socket");
        return -1;
    }

    freeaddrinfo(servinfo);

    if(listen(server_fd, 1) != 0)
    {
        perror("socket");
        syslog(LOG_ERR,"failed to listen");
        close(server_fd);
        return -1;
    }

    // Accept connection
    client_fd = accept(server_fd, &client_addr, &client_addr_len);
    if (client_fd == -1) {
        perror("accept");
        syslog(LOG_ERR, "Failed accepting the connection");
        close(server_fd);
        return -1;
    }
    
    FILE *file = NULL;
    file = fopen("/var/tmp/aesdsocketdata", "w");

    if (file == NULL)
    {
        syslog(LOG_ERR,"failed to open file");
        exit(1);
    }

    while(1)
    {
        ssize_t bytes_received;
        while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0)
        {
            char *temp = realloc(rxdata, rxdata_len + bytes_received);
            if(temp == NULL)
            {
                syslog(LOG_ERR, "realloc error");
                break;
            }
            rx_data = temp;
            memcpy(rxdata + rxdata_len, buffer, bytes_received);
            rxdata_len += bytes_received;

            while ((newline_pos = memchr(received_data, '\n', received_data_len)) != NULL) 
            {
                size_t packet_len = newline_pos - received_data + 1;
                if (write(data_fd, received_data, packet_len) == -1) 
                {
                    syslog(LOG_ERR, "Error writing to data file: %s", strerror(errno));
                }

                lseek(data_fd, 0, SEEK_SET);

                char *file_content = NULL;
                size_t file_size = 0;
                ssize_t read_size;
                while ((read_size = read(data_fd, buffer, BUFFER_SIZE)) > 0) 
                {
                    char *temp_file = realloc(file_content, file_size + read_size);
                    if(temp_file == NULL)
                    {
                        syslog(LOG_ERR, "realloc error");
                        break;
                    }
                    file_content = temp_file;
                    memcpy(file_content + file_size, buffer, read_size);
                    file_size += read_size;
                }
                if(file_content != NULL)
                {
                    send(client_fd, file_content, file_size, 0);
                    free(file_content);
                }
            }


        }
    }
    if (fclose(file) != 0) 
    {
        exit(1);
    }
    return 0;
}