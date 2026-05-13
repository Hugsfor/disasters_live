#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

// Variabile globale preluate din monitorizările active din main.c
extern double global_earthquake_risk;
extern double global_flood_risk;

// Funcție ajutătoare pentru a converti probabilitatea numerică în textul cerut de legendă
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
    if (server_fd < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close_socket(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close_socket(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("HTTP Server running on port %d (GeoJSON Dispatcher)\n", PORT);

    while (1) {
        int addrlen = sizeof(address);
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        
        if (new_socket < 0) continue;

        char buffer[2048] = {0};
        recv(new_socket, buffer, sizeof(buffer) - 1, 0);

        // Generăm dinamic JSON-ul cu structura exactă pe care o așteaptă scriptul din index.html
        char json[2048];
        time_t timestamp = time(NULL);
        
        int json_len = snprintf(json, sizeof(json),
            "{\n"
            "  \"events\": [\n"
            "    {\n"
            "      \"type\": \"earthquake\",\n"
            "      \"risk\": \"%s\",\n"
            "      \"probability\": %.2f,\n"
            "      \"magnitude\": %.1f,\n"
            "      \"affected_radius\": %.1f,\n"
            "      \"latitude\": 34.05,\n"
            "      \"longitude\": -118.25,\n"
            "      \"detection_time\": %ld\n"
            "    },\n"
            "    {\n"
            "      \"type\": \"flood\",\n"
            "      \"risk\": \"%s\",\n"
            "      \"probability\": %.2f,\n"
            "      \"magnitude\": %.1f,\n"
            "      \"affected_radius\": %.1f,\n"
            "      \"latitude\": 35.20,\n"
            "      \"longitude\": -97.44,\n"
            "      \"detection_time\": %ld\n"
            "    }\n"
            "  ]\n"
            "}",
            get_risk_string(global_earthquake_risk), global_earthquake_risk, (global_earthquake_risk * 9.0), (global_earthquake_risk * 150.0), (long)timestamp,
            get_risk_string(global_flood_risk), global_flood_risk, (global_flood_risk * 5.0), (global_flood_risk * 80.0), (long)timestamp
        );

        // Construim răspunsul HTTP, incluzând CORS pentru a permite browserului să citească datele cross-origin
        char response[4096];
        int response_len = snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s",
            json_len, json
        );

        send(new_socket, response, response_len, 0);
        close_socket(new_socket);
    }

#ifdef _WIN32
    WSACleanup();
#endif
}
