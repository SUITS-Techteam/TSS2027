#include "server.h"

struct profile_context_t profile_context;
static bool debug_mode = false;

// Static function declarations
static bool continue_server(void);
static void get_contents(char *buffer, unsigned int *time, unsigned int *command,
                         unsigned char *data, int packet_size);

int main(int argc, char *argv[]) {

    // Check for debug mode argument
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
            printf("Debug mode enabled\n");
            break;
        }
    }

    // Setup high precision timing
    clock_setup(&profile_context);

    // Ignore SIGPIPE signal on Unix-like systems to prevent crashes on broken pipes, this was caused before by constantly refreshing the webpage
    #if !defined(_WIN32)
        signal(SIGPIPE, SIG_IGN);
    #endif

    // Windows specific initialization
    #if defined(_WIN32)
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d)) {
            printf("Failed to initialize WSA\n");
            return -1;
        }
    #endif

    // Fetch server hostname and port to bind to
    char hostname[16];
    char port[6] = "14141";
    get_ip_address(hostname);

    printf("Launching Server at IP: %s:%s\n", hostname, port);

    // Create TCP and UDP sockets for serving the website and handling UDP data requests
    SOCKET server;
    SOCKET main_udp_socket;

    server = create_tcp_socket(hostname, port);
    main_udp_socket = create_udp_socket(hostname, port);

    SOCKET* udp_sockets = malloc(sizeof(SOCKET) * NUM_TEAMS);
    // Initialize backends data system
    struct backend_data_t **backends = malloc(sizeof(struct backend_data_t*) * NUM_TEAMS);
    for (int i = 0; i < 10; i++) {
        int port = 14141 + 1 + i;
        char char_port[10];

        sprintf(char_port, "%d", port);
        udp_sockets[i] = create_tcp_socket(hostname, char_port);
        backends[i] = init_backend(i);
    }



    // Initialize client connection list
    struct client_info_t *clients = NULL;

    // Main server loop
    while (true) {
        fd_set reads;

        // Handle new TCP client connections
        reads = wait_on_clients(clients, server, main_udp_socket);

        if (FD_ISSET(server, &reads)) {
            struct client_info_t *client = get_client(&clients, -1);
            if (!client) {
                fprintf(stderr, "Failed to allocate memory for new client connection\n");
                continue;
            }

            // Accept the new connection
            client->socket =
                accept(server, (struct sockaddr *)&client->address, &client->address_length);
            if (!ISVALIDSOCKET(client->socket)) {
                fprintf(stderr, "accept() failed with error: %d\n", GETSOCKETERRNO());
                drop_tcp_client(&clients, client);
                continue;
            }
        }

         // Handle existing TCP client requests
        struct client_info_t *client = clients;

        while (client) {
            struct client_info_t *next_client = client->next;

            // Process data from this client
            if (FD_ISSET(client->socket, &reads)) {

                // Check for buffer overflow on request, send 400 and drop client if so
                if (MAX_REQUEST_SIZE <= client->received) {
                    send_400(client);
                    drop_tcp_client(&clients, client);
                    client = next_client;
                    continue;
                }

                // Read incoming data from client
                int bytes_received = recv(client->socket, client->request + client->received,
                                        MAX_REQUEST_SIZE - client->received, 0);

                if (bytes_received < 1) {
                    // Connection closed or error
                    fprintf(stderr, "Unexpected Disconnect from %s\n", get_client_address(client));
                    drop_tcp_client(&clients, client);
                } else {
                    client->received += bytes_received;
                    client->request[client->received] = 0;

                    // Check if we have a complete HTTP request
                    char *q = strstr(client->request, "\r\n\r\n");
                    if (q) { 
                        if (strncmp(client->request, "GET /", 5) == 0) { // HTTP GET request
                            char *path = client->request + 4;
                            char *end_path = strstr(path, " ");

                            if (!end_path) {
                                // Malformed request
                                send_400(client);
                                drop_tcp_client(&clients, client);
                            } else {
                                // Null-terminate the path and serve the resource
                                *end_path = 0;
                                serve_resource(client, path);
                                drop_tcp_client(&clients, client);
                            }
                        } else if (strncmp(client->request, "POST /", 6) == 0) { // HTTP POST request
                            // Parse Content-Length header
                            if (client->message_size == -1) {
                                char *request_content_size_ptr =
                                    strstr(client->request, "Content-Length: ");
                                if (request_content_size_ptr) {
                                    request_content_size_ptr += strlen("Content-Length: ");
                                    client->message_size =
                                        atoi(request_content_size_ptr) + (q - client->request) + 4;
                                } else {
                                    // Missing Content-Length header
                                    send_400(client);
                                    drop_tcp_client(&clients, client);
                                }
                            }

                            if (client->received == client->message_size) {
                                // Complete POST request received
                                char *request_content = strstr(client->request, "\r\n\r\n");
                                request_content += 4;  // Skip past header delimiter
                                //get team index
                                while (*request_content != '=') {
                                    request_content++;
                                }
                                request_content++;
                                //Only handles single digit team numbers
                                int instance_index = *request_content - '0';
                                request_content += 2;

                                if (!request_content) {
                                    send_400(client);
                                    drop_tcp_client(&clients, client);
                                } else {
                                    printf("%s\n", request_content);
                                    if (html_form_json_update(request_content, backends[instance_index])) {
                                        send_304(client);
                                    } else {
                                        send_400(client);
                                    }
                                    drop_tcp_client(&clients, client);
                                }
                            }

                        } else { //= Unsupported HTTP methods
                            send_400(client);
                            drop_tcp_client(&clients, client);
                        }
                    }
                }
            }

            // Move to the next client in the list
            client = next_client;
        }

        // Handle UDP datagram packets
        for (int i = 0; i < NUM_TEAMS; i++) {
            if (FD_ISSET(udp_sockets[i], &reads)) {
                struct client_info_t *udp_clients = NULL;
                struct client_info_t *client = get_client(&udp_clients, -1);
                if (!client) {
                    fprintf(stderr, "Failed to allocate memory for UDP client\n");
                    continue;
                }

                int received_bytes =
                    recvfrom(udp_sockets[i], client->udp_request, MAX_UDP_REQUEST_SIZE, 0,
                            (struct sockaddr *)&client->udp_addr, &client->address_length);


                // Always interpret UDP packets as big-endian
                if (!big_endian()) {
                    // System is little-endian, so convert big-endian UDP to little-endian
                    reverse_bytes(client->udp_request);     // timestamp (bytes 0-3)
                    reverse_bytes(client->udp_request + 4); // command (bytes 4-7)

                    // Only convert value bytes if this is a POST request (12 bytes)
                    if (received_bytes >= 12) {
                        reverse_bytes(client->udp_request + 8); // value (bytes 8-11)
                    }
                }

                unsigned int time = 0;
                unsigned int command = 0;
                char data[4] = {0};

                get_contents(client->udp_request, &time, &command, data, received_bytes);

                // @TODO the code below could definitely be simplified further, although for the sake of clarity it's left as is for now

                // Process UDP command based on command range
                if (command < 1000) {  // GET requests
                    unsigned char *response_buffer;
                    int buffer_size = 0;

                    // Allocate buffer for JSON response (8 bytes header + JSON data)
                    char json_data[20000] = {0};  // Buffer for JSON content
                    handle_udp_get_request(command, (unsigned char *)json_data, backends[i]);

                    size_t json_len = strlen(json_data);
                    buffer_size = json_len;
                    response_buffer = malloc(buffer_size);
                    if (!response_buffer) {
                        fprintf(stderr, "Failed to allocate memory for GET response\n");
                        drop_udp_client(&udp_clients, client);
                        continue;
                    }

                    // Prepare response packet with JSON data content
                    memcpy(response_buffer, json_data, json_len);

                    // Send response
                    int bytes_sent =
                        sendto(udp_sockets[i], response_buffer, buffer_size, 0,
                            (struct sockaddr *)&client->udp_addr, client->address_length);


                    // UDP requests are one-off, so drop client after the response
                    drop_udp_client(&udp_clients, client);
                    free(response_buffer);
                } else if (command < 3000) {  // POST requests, primarily the TSS peripherals
                    //TODO: Move all post commands to the main_udp_socket and use perphial mapping to control which backend to send data to
                    bool result = handle_udp_post_request(command, (unsigned char *)data, backends[i]);
                
                    // Send status of POST request back to client with just boolean response flag
                    unsigned char response_buffer[4];
                    unsigned int status = result ? 1 : 0;
                    memcpy(response_buffer, &status, 4);
                    sendto(udp_sockets[i], response_buffer, sizeof(response_buffer), 0,
                        (struct sockaddr *)&client->udp_addr, client->address_length);

                    drop_udp_client(&udp_clients, client);
                } else {  // Unknown command
                    drop_udp_client(&udp_clients, client);
                }
            }
        }

        // Check if user requested server shutdown by pressing ENTER
        if (!continue_server()) {
            break;
        }

        for (int i = 0; i < NUM_TEAMS; i++) {
            // Update simulation state based on the elapsed time
            increment_simulation(backends[i]);

            // Sync simulation data to JSON files
            sync_simulation_to_json(backends[i]);
        }
    }

    // Cleanup phase - shutdown server gracefully
    printf("Clean up Database...\n");
    for (int i = 0; i < NUM_TEAMS; i++) {
        cleanup_backend(backends[i]);
        CLOSESOCKET(udp_sockets[i]);
    }

    printf("Closing Sockets...\n");
    CLOSESOCKET(server);

    // Windows specific socket close
    #if defined(_WIN32)
    #else
        for (int i = 0; i < NUM_TEAMS; i++) {
            close(udp_sockets[i]);
        }
    #endif

    printf("Cleaned up server listen sockets\n");
    int leftover_clients = 0;
    struct client_info_t *client = clients;
    while (client) {
        drop_tcp_client(&clients, client);
        leftover_clients++;
        client = client->next;
    }
    printf("Cleaned up %d client sockets\n", leftover_clients);

    // Windows specific cleanup
    #if defined(_WIN32)
        WSACleanup();
    #endif

    printf("\nGoodbye World\n");
    return 0;
}

/**
 * Checks if the user wants to stop the server by pressing ENTER.
 * Non-blocking check using select() with zero timeout.
 * 
 * @return false if ENTER was pressed, true to continue running
 */
static bool continue_server(void) {
    struct timeval select_wait;
    select_wait.tv_sec = 0;
    select_wait.tv_usec = 0;
    fd_set console_fd;

    FD_ZERO(&console_fd);
    FD_SET(0, &console_fd);
    int read_files = select(1, &console_fd, 0, 0, &select_wait);

    if (read_files > 0) {
        return false;
    } else {
        return true;
    }
}

/**
 * Extracts UDP packet contents into separate fields.
 * UDP packet format: [time:4][command:4][data:4]
 * 
 * @param buffer Raw UDP packet data
 * @param time Pointer to store timestamp
 * @param command Pointer to store command code
 * @param data Pointer to store 4-byte data payload
 */
static void get_contents(char *buffer, unsigned int *time, unsigned int *command,
                         unsigned char *data, int packet_size) {
    // Always extract timestamp and command (present in both GET and POST)
    memcpy(time, buffer, 4);
    memcpy(command, buffer + 4, 4);

    // Only extract data for POST requests (12 bytes total)
    if (packet_size >= 12) {
        memcpy(data, buffer + 8, 4);
    } else {
        // For GET requests, clear the data buffer
        memset(data, 0, 4);
    }
}
