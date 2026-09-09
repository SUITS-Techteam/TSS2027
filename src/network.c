// network.c - handles all socket operations, HTTP server functionality

#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/**
 * Initializes high-precision timing for the given platform.
 * Sets up performance counters on Windows, monotonic clock on Linux,
 * and uptime clock on macOS for accurate time measurements.
 */
void clock_setup(struct profile_context_t *ptContext) {
    #ifdef _WIN32
        static LARGE_INTEGER perf_freq;
        QueryPerformanceFrequency(&perf_freq);
        ptContext->pInternal = &perf_freq;

    #elif defined(__APPLE__)
        ptContext->pInternal = NULL;

    #else  // Linux
        static struct timespec ts;
        static double dPerFrequency = 0;

        if (dPerFrequency == 0) {  // Ensure it's initialized only once
            if (clock_getres(CLOCK_MONOTONIC, &ts) != 0) {
                fprintf(stderr, "clock_getres() failed\n");
                exit(1);
            }
            double total_nsec = (double)ts.tv_nsec + (double)ts.tv_sec * 1e9;
            if (total_nsec == 0) {
                fprintf(stderr, "Invalid clock resolution (division by zero)\n");
                exit(1);
            }
            dPerFrequency = 1e9 / total_nsec;
        }

        ptContext->pInternal = &dPerFrequency;
    #endif
}


/**
 * Discovers the first non-loopback IP address of the local machine.
 * Falls back to 127.0.0.1 if no network interface is found.
 * 
 * @param hostname_out Buffer to store the IP address (minimum 16 bytes)
 */
void get_ip_address(char *hostname_out) {
    if (!hostname_out) {
        fprintf(stderr, "get_ip_address: hostname_out is NULL\n");
        return;
    }
    
    memset(hostname_out, 0, 16);
    strcpy(hostname_out, "127.0.0.1"); // set default to localhost

#if defined(_WIN32)
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    DWORD dwRetVal = 0;

    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
    pAdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
    if (pAdapterInfo == NULL) {
        fprintf(stderr, "Error allocating memory needed to call GetAdaptersinfo\n");
        return;
    }
    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO *)malloc(ulOutBufLen);
        if (pAdapterInfo == NULL) {
            fprintf(stderr, "Error allocating memory needed to call GetAdaptersinfo\n");
            return;
        }
    }

    if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR) {
        pAdapter = pAdapterInfo;
        while (pAdapter) {
            char *ipAddr = pAdapter->IpAddressList.IpAddress.String;
            if (strcmp(ipAddr, "127.0.0.1") != 0 && strcmp(ipAddr, "0.0.0.0") != 0) {
                memset(hostname_out, 0, 16);
                strcpy(hostname_out, ipAddr);
            }

            pAdapter = pAdapter->Next;
        }
    } else {
        printf("GetAdaptersInfo failed with error: %d\n", dwRetVal);
    }
    if (pAdapterInfo)
        free(pAdapterInfo);

#else
    struct ifaddrs *addresses;
    if (getifaddrs(&addresses) == -1) {
        printf("getifaddrs call failed\n");
        return;
    }

    struct ifaddrs *address = addresses;
    while (address) {
        if (address->ifa_addr == NULL) {
            address = address->ifa_next;
            continue;
        }

        int family = address->ifa_addr->sa_family;
        if (family == AF_INET) {
            char ap[100];
            const int family_size = sizeof(struct sockaddr_in);
            getnameinfo(address->ifa_addr, family_size, ap, sizeof(ap), 0, 0, NI_NUMERICHOST);
            if (strcmp(ap, "127.0.0.1") != 0) {
                memset(hostname_out, 0, 16);
                strcpy(hostname_out, ap);
            }
        }
        address = address->ifa_next;
    }
    freeifaddrs(addresses);
#endif
}

/**
 * Determines the MIME content type based on file extension.
 * Used for HTTP responses to set proper Content-Type headers.
 * 
 * @param path File path with extension
 * @return MIME type string, defaults to "application/octet-stream"
 */
const char *get_content_type(const char *path) {
    if (!path) {
        return "application/octet-stream";
    }
    
    const char *last_dot = strstr(path, ".");
    if (last_dot) {
        if (strcmp(last_dot, ".css") == 0)
            return "text/css";
        if (strcmp(last_dot, ".csv") == 0)
            return "text/csv";
        if (strcmp(last_dot, ".gif") == 0)
            return "image/gif";
        if (strcmp(last_dot, ".htm") == 0)
            return "text/html";
        if (strcmp(last_dot, ".html") == 0)
            return "text/html";
        if (strcmp(last_dot, ".ico") == 0)
            return "image/x-icon";
        if (strcmp(last_dot, ".jpeg") == 0)
            return "image/jpeg";
        if (strcmp(last_dot, ".jpg") == 0)
            return "image/jpeg";
        if (strcmp(last_dot, ".js") == 0)
            return "application/javascript";
        if (strcmp(last_dot, ".json") == 0)
            return "application/json";
        if (strcmp(last_dot, ".png") == 0)
            return "image/png";
        if (strcmp(last_dot, ".svg") == 0)
            return "image/svg+xml";
        if (strcmp(last_dot, ".txt") == 0)
            return "text/plain";
    }

    return "application/octet-stream";
}

/**
 * Creates and binds a UDP socket for datagram communication.
 * Used for real-time telemetry data exchange with clients.
 * 
 * @param hostname IP address to bind to
 * @param port Port number to bind to
 * @return Socket descriptor, or -1 on failure
 */
SOCKET create_udp_socket(char *hostname, char *port) {
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(hostname);
    server_addr.sin_port = htons(atoi(port));
    SOCKET server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket == -1) {
        perror("Failed to create socket.");

        return -1;
    }

    int bind_result = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (bind_result == -1) {
        perror("Failed to bind...");

        return -1;
    }


    return server_socket;
}

/**
 * Creates and configures a TCP listening socket for HTTP connections.
 * Sets up socket options for reuse and begins listening for connections.
 * 
 * @param hostname IP address to bind to
 * @param port Port number to bind to  
 * @return Socket descriptor, exits on failure
 */
SOCKET create_tcp_socket(char *hostname, char *port) {
    printf("Configuring Local Address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *bind_address;
    if (getaddrinfo(hostname, port, &hints, &bind_address)) {
        fprintf(stderr, "getaddrinfo() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }

    printf("Creating HTTP Socket...\n");
    SOCKET socket_listen;
    socket_listen =
        socket(bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol);
    if (!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }

    // Enable socket reuse to avoid "Address already in use" errors
    char c = 1;
    setsockopt(socket_listen, SOL_SOCKET, SO_REUSEADDR, &c, sizeof(int));

    printf("Binding HTTP Socket...\n");
    if (bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen)) {
        fprintf(stderr, "bind() failed with error: %d", GETSOCKETERRNO());
        CLOSESOCKET(socket_listen);
        exit(1);
    }

    printf("Listening to HTTP Socket...\n");
    if (listen(socket_listen, 10) > 0) {
        fprintf(stderr, "listen() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(bind_address);

    return socket_listen;
}

/**
 * Finds an existing client by socket or creates a new client entry.
 * Maintains a linked list of active client connections.
 * 
 * @param clients Pointer to head of client list
 * @param socket Socket descriptor to search for
 * @return Client structure, newly allocated if not found
 */
struct client_info_t *get_client(struct client_info_t **clients, SOCKET socket) {
    // look for the client in the linked list
    struct client_info_t *client = *clients;
    while (client) {
        if (client->socket == socket) {
            return client;
        }
        client = client->next;
    }

    // client not found, make one
    struct client_info_t *new_client = (struct client_info_t *)malloc(sizeof(struct client_info_t));
    if (new_client == NULL) {
        fprintf(stderr, "Failed to allocate memory for new client\n");
        return NULL;
    }

    // Zero-initialize struct, especially the request buffer for null termination
    memset(new_client, 0, sizeof(struct client_info_t));

    new_client->address_length = sizeof(new_client->address);
    new_client->received = 0;
    new_client->message_size = -1;

    new_client->next = *clients;
    *clients = new_client;

    return new_client;
}

/**
 * Removes a UDP client from the client list without closing the socket.
 * UDP clients don't maintain persistent connections like TCP clients.
 */
void drop_udp_client(struct client_info_t **clients, struct client_info_t *client) {
    struct client_info_t **p = clients;

    while (*p) {
        if (*p == client) {
            *p = client->next;
            free(client);
            return;
        }
        p = &((*p)->next);
    }

    fprintf(stderr, "drop_client: client not found\n");
    exit(1);
}

/**
 * Removes a TCP client, closes the socket, and frees memory.
 * Maintains the integrity of the client linked list.
 */
void drop_tcp_client(struct client_info_t **clients, struct client_info_t *client) {
    CLOSESOCKET(client->socket);

    struct client_info_t **p = clients;

    while (*p) {
        if (*p == client) {
            *p = client->next;
            free(client);
            return;
        }
        p = &((*p)->next);
    }

    fprintf(stderr, "drop_client: client not found\n");
    exit(1);
}

/**
 * Converts client's TCP socket address to a readable IP string.
 * Uses a static buffer, so subsequent calls overwrite previous results.
 * 
 * @param client Client structure with address information
 * @return IP address string or "unknown" on error
 */
const char *get_client_address(struct client_info_t *client) {
    static char address_buffer[100];
    
    if (!client) {
        strcpy(address_buffer, "unknown");
        return address_buffer;
    }
    
    if (getnameinfo((struct sockaddr *)&client->address, client->address_length, address_buffer,
                    sizeof(address_buffer), 0, 0, NI_NUMERICHOST) != 0) {
        strcpy(address_buffer, "unknown");
    }
    
    return address_buffer;
}

/**
 * Converts client's UDP address to a readable IP string.
 * Separate from TCP address as UDP clients can have different addressing.
 * 
 * @param client Client structure with UDP address information  
 * @return IP address string or "unknown" on error
 */
const char *get_client_udp_address(struct client_info_t *client) {
    static char address_buffer[100];
    
    if (!client) {
        strcpy(address_buffer, "unknown");
        return address_buffer;
    }
    
    if (getnameinfo((struct sockaddr *)&client->udp_addr, client->address_length, address_buffer,
                    sizeof(address_buffer), 0, 0, NI_NUMERICHOST) != 0) {
        strcpy(address_buffer, "unknown");
    }
    
    return address_buffer;
}

/**
 * Sets up a select() call to monitor multiple sockets for activity.
 * Monitors the server listening socket, UDP socket, and all client connections.
 * 
 * @param clients Linked list of active clients
 * @param server listening socket
 * @return File descriptor set with sockets ready for I/O
 */
fd_set wait_on_clients(struct client_info_t *clients, SOCKET socket) {
    // Non-blocking select with 100ms timeout for responsive server loop
    struct timeval select_wait;
    select_wait.tv_sec = 0;
    select_wait.tv_usec = 100000;

    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(socket, &reads);
    SOCKET max_socket = socket;

    struct client_info_t *client = clients;
    while (client) {
        FD_SET(client->socket, &reads);
        if (client->socket > max_socket) {
            max_socket = client->socket;
        }
        client = client->next;
    }

    if (select(max_socket + 1, &reads, 0, 0, &select_wait) < 0) {
        fprintf(stderr, "select() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }

    return reads;
}

/**
 * Sends HTTP 400 Bad Request response to client.
 * Used for malformed requests or invalid data.
 */
void send_400(struct client_info_t *client) {
    const char *c400 =
        "HTTP/1.1 400 Bad Request\r\n"
        "Connection: close\r\n"
        "Content-Length: 10\r\n\r\nBadRequest";

    send(client->socket, c400, strlen(c400), 0);
}

/**
 * Sends HTTP 404 Not Found response to client.
 * Used when requested resource doesn't exist.
 */
void send_404(struct client_info_t *client) {
    const char *c404 =
        "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "Content-Length: 9\r\n\r\nNot Found";

    send(client->socket, c404, strlen(c404), 0);
}

/**
 * Sends HTTP 201 Created response to client.
 * Used after successfully creating a new resource.
 */
void send_201(struct client_info_t *client) {
    const char *c201 =
        "HTTP/1.1 201 Created\r\n"
        "Connection: close\r\n"
        "Content-Length: 7\r\n\r\nCreated";

    send(client->socket, c201, strlen(c201), 0);
}

/**
 * Sends HTTP 304 Not Modified response to client.
 * Used when resource hasn't changed since last request.
 */
void send_304(struct client_info_t *client) {
    const char *c304 =
        "HTTP/1.1 304 Not Modified\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: 12\r\n\r\nNot Modified";

    send(client->socket, c304, strlen(c304), 0);
}

/**
 * Resets client's request buffer for reuse on persistent connections.
 * Allows the same TCP connection to handle multiple HTTP requests.
 */
void reset_client_request_buffer(struct client_info_t *client) {
    // This allows a client to send a new request over the same socket
    client->received = 0;
    memset(client->request, 0, MAX_REQUEST_SIZE);
}

/**
 * Serves static files from the frontend directory via HTTP.
 * Handles path validation, file reading, and proper HTTP headers.
 * 
 * @param client Client requesting the resource
 * @param path Requested file path (relative to frontend/)
 */
void serve_resource(struct client_info_t *client, const char *path) {
    if (!client || !path) {
        if (client) send_400(client);
        return;
    }

    if (strcmp(path, "/") == 0) {
        path = "/index.html";
    }

    if (strlen(path) > 100) {
        send_400(client);
        return;
    }

    // Prevent directory traversal attacks
    if (strstr(path, "..")) {
        send_404(client);
        return;
    }

    char full_path[128];

    // Check if the request is for data files
    if (strncmp(path, "/data/", 6) == 0) {
        // Serve from root data directory, strip the leading slash
        sprintf(full_path, "%s", path + 1);
    } else {
        // Serve from frontend directory
        sprintf(full_path, "frontend%s", path);
    }

    // Convert Unix-style paths to Windows paths when needed
#if defined(_WIN32)
    char *p = full_path;
    while (*p) {
        if (*p == '/') {
            *p = '\\';
        }
        p++;
    }
#endif

    FILE *fp = fopen(full_path, "rb");

    if (!fp) {
        send_404(client);
        return;
    }

    // Determine file size for Content-Length header
    fseek(fp, 0L, SEEK_END);
    size_t content_length = ftell(fp);
    rewind(fp);

    const char *content_type = get_content_type(full_path);

#define BSIZE 1024  // Buffer size for file reading and HTTP headers

    char content_buffer[BSIZE];

    sprintf(content_buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, content_buffer, strlen(content_buffer), 0);

    sprintf(content_buffer, "Connection: close\r\n");
    send(client->socket, content_buffer, strlen(content_buffer), 0);

    sprintf(content_buffer, "Content-Length: %lu\r\n", content_length);
    send(client->socket, content_buffer, strlen(content_buffer), 0);

    sprintf(content_buffer, "Content-Type: %s\r\n", content_type);
    send(client->socket, content_buffer, strlen(content_buffer), 0);

    sprintf(content_buffer, "\r\n");
    send(client->socket, content_buffer, strlen(content_buffer), 0);

    // Send file content in chunks
    int bytes_read = fread(content_buffer, 1, BSIZE, fp);
    while (bytes_read) {
        send(client->socket, content_buffer, bytes_read, 0);
        bytes_read = fread(content_buffer, 1, BSIZE, fp);
    }

    fclose(fp);
}

