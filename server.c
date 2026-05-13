#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>

#define socklen_t int

#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#endif

#define PORT 8080

void start_server() {

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    int server_fd;
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));

    listen(server_fd, 3);

    printf("HTTP Server running on port %d\n", PORT);

    while (1) {

        int addrlen = sizeof(address);

        int new_socket =
            accept(server_fd, (struct sockaddr *)&address,
                   (socklen_t*)&addrlen);

        char buffer[30000] = {0};

        recv(new_socket, buffer, sizeof(buffer), 0);

        printf("%s\n", buffer);

        const char *json =
            "{ \"earthquake\": 0.72, \"flood\": 0.21 }";

        char response[1024];

        sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s",
            (int)strlen(json),
            json
        );

        send(new_socket, response, strlen(response), 0);

#ifdef _WIN32
        closesocket(new_socket);
#else
        close(new_socket);
#endif
    }
}

