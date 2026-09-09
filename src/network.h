#ifndef NETWORK_H
#define NETWORK_H

///////////////////////////////////////////////////////////////////////////////////
//                              Platform Dependent Headers
///////////////////////////////////////////////////////////////////////////////////

#if defined(_WIN32)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <iphlpapi.h>
    #include <WS2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    typedef int socklen_t;

    #define ISVALIDSOCKET(s) ((s) != INVALID_SOCKET)
    #define CLOSESOCKET(s) closesocket(s)
    #define GETSOCKETERRNO() (WSAGetLastError())
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <ifaddrs.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>

    #define SOCKET int
    #define ISVALIDSOCKET(s) ((s) >= 0)
    #define CLOSESOCKET(s) close(s)
    #define GETSOCKETERRNO() (errno)
#endif

#include <math.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////
//                                  Structs
///////////////////////////////////////////////////////////////////////////////////

#define MAX_REQUEST_SIZE 2047
#define MAX_UDP_REQUEST_SIZE 8008

struct client_info_t {
    socklen_t address_length;
    struct sockaddr_storage address;
    struct sockaddr_in udp_addr;
    SOCKET socket;
    char request[MAX_REQUEST_SIZE+1];
    char udp_request[MAX_UDP_REQUEST_SIZE];
    int received;
    int message_size;
    double last_request_time;
    struct client_info_t* next;
};

extern struct profile_context_t profile_context;
///////////////////////////////////////////////////////////////////////////////////
//                                 Functions
///////////////////////////////////////////////////////////////////////////////////

void get_ip_address(char* hostname_out);
const char* get_content_type(const char* path);
SOCKET create_tcp_socket(char* hostname, char* port);
SOCKET create_udp_socket(char* hostname, char* port);
struct client_info_t* get_client(struct client_info_t** clients, SOCKET socket);
void drop_udp_client(struct client_info_t** clients, struct client_info_t* client);
void drop_tcp_client(struct client_info_t** clients, struct client_info_t* client);
const char* get_client_address(struct client_info_t* client);
const char* get_client_udp_address(struct client_info_t* client);
fd_set wait_on_clients(struct client_info_t* clients, SOCKET socket);
void send_400(struct client_info_t* client);
void send_404(struct client_info_t* client);
void send_201(struct client_info_t* client);
void send_304(struct client_info_t* client);
void reset_client_request_buffer(struct client_info_t* client);
void serve_resource(struct client_info_t* client, const char* path);

///////////////////////////////////////////////////////////////////////////////////
//                        Cross-Platform High-Precision Timing
///////////////////////////////////////////////////////////////////////////////////

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <time.h>
#else
    #include <time.h>
    #include <stdint.h>
#endif

struct profile_context_t {
    void* pInternal;
};

void clock_setup(struct profile_context_t* ptContext);

static inline double get_wall_clock(struct profile_context_t* profileContext) {

    if (!profileContext || !profileContext->pInternal){
        fprintf(stderr, "[ERROR] get_wall_clock: profileContext or pInternal is NULL\n");
        return 0.0;
    }

    double dResult = 0;

    #ifdef _WIN32
        INT64 slPerfFrequency = ((LARGE_INTEGER*)profileContext->pInternal)->QuadPart;
        INT64 slPerfCounter;
        QueryPerformanceCounter((LARGE_INTEGER*)&slPerfCounter);
        dResult = (double)slPerfCounter / (double)slPerfFrequency;
    #elif defined(__APPLE__)
        dResult = ((double)(clock_gettime_nsec_np(CLOCK_UPTIME_RAW)) / 1e9);
    #else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t nsec_count = ts.tv_nsec + ts.tv_sec * 1000000000ULL;
        double frequency = *(double*)profileContext->pInternal;
        dResult = (double)nsec_count / frequency;
    #endif

    return dResult;
}

#endif // NETWORK_H
