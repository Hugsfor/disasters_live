#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#ifdef _WIN32
#include <winsock2.h>
#define socklen_t int
#define close_socket(s) closesocket(s)
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#define close_socket(s) close(s)
#endif

#define PORT 8080


#include "headers/global.h"

extern double global_earthquake_risk;
extern double global_flood_risk;

const char* get_risk_string(double prob) {
    if (prob > 0.75) return "critical";
    if (prob > 0.50) return "high";
    if (prob > 0.25) return "elevated";
    return "normal";
}

void start_server() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return;
    }
#endif

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("Socket failed"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed"); close_socket(server_fd); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed"); close_socket(server_fd); exit(EXIT_FAILURE);
    }

    printf("HTTP Server running on port %d\n", PORT);

    while (1) {
        int addrlen = sizeof(address);
        int new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) continue;

        char buffer[2048] = {0};
        recv(new_socket, buffer, sizeof(buffer) - 1, 0);

        // Build JSON from the real events array (filled by data_parser.c)
        char *json = calloc(1, 200000);
        strcpy(json, "{\n  \"events\": [\n");

        pthread_mutex_lock(&events_mutex);
        int count = global_events_count;

        for (int i = 0; i < count; i++) {
            char item[512];
            snprintf(item, sizeof(item),
                "    {\n"
                "      \"type\": \"%s\",\n"
                "      \"risk\": \"%s\",\n"
                "      \"probability\": %.2f,\n"
                "      \"magnitude\": %.1f,\n"
                "      \"affected_radius\": %.1f,\n"
                "      \"latitude\": %.4f,\n"
                "      \"longitude\": %.4f,\n"
                "      \"detection_time\": %ld\n"
                "    }%s\n",
                global_events_list[i].type,
                get_risk_string(global_events_list[i].probability),
                global_events_list[i].probability,
                global_events_list[i].magnitude,
                global_events_list[i].affected_radius,
                global_events_list[i].latitude,
                global_events_list[i].longitude,
                (long)time(NULL),
                (i == count - 1) ? "" : ","
            );
            if (strlen(json) + strlen(item) < 180000)
                strcat(json, item);
        }

        pthread_mutex_unlock(&events_mutex);
        strcat(json, "  ]\n}");

        int body_len = strlen(json);
        char header[512];
        sprintf(header,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            body_len);

        send(new_socket, header, strlen(header), 0);
        send(new_socket, json, body_len, 0);
        free(json);
        close_socket(new_socket);
    }

#ifdef _WIN32
    WSACleanup();
#endif
}
