
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 16384

static int send_all(SOCKET client, const char *data, int length)
{
    int sent = 0;

    while (sent < length) {
        int n = send(client, data + sent, length - sent, 0);
        if (n == SOCKET_ERROR || n == 0)
            return 0;
        sent += n;
    }

    return 1;
}

static void send_response(
    SOCKET client,
    int status,
    const char *reason,
    const char *body)
{
    char header[512];

    int body_len = (int)strlen(body);

    int header_len = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, reason, body_len
    );

    if (header_len > 0 && header_len < (int)sizeof(header)) {
        if (send_all(client, header, header_len))
            send_all(client, body, body_len);
    }
}

static void handle_request(SOCKET client)
{
    char buffer[BUFFER_SIZE];
    int used = 0;
    int header_end = -1;
    int content_length = 0;
    // Create a vector to hold the request body data

    memset(buffer, 0, sizeof(buffer));

    /* Read the HTTP headers. */
    while (used < BUFFER_SIZE - 1 && header_end < 0) {
        int n = recv(
            client, buffer + used,
            BUFFER_SIZE - 1 - used, 0
        );

        if (n <= 0)
            return;

        used += n;
        buffer[used] = '\0';

        char *end = strstr(buffer, "\r\n\r\n");
        if (end != NULL)
            header_end = (int)(end - buffer) + 4;
    }

    if (header_end < 0) {
        send_response(client, 400, "Bad Request",
                      "{\"error\":\"Invalid HTTP headers\"}");
        return;
    }

    /* Extract the request method and path. */
    char method[16] = {0};
    char path[256] = {0};

    if (sscanf(buffer, "%15s %255s", method, path) != 2) {
        send_response(client, 400, "Bad Request",
                      "{\"error\":\"Invalid request line\"}");
        return;
    }

    /* Read the declared request body, if present. */
    char *cl = strstr(buffer, "\r\nContent-Length:");
    if (cl == NULL)
        cl = strstr(buffer, "\r\ncontent-length:");

    if (cl != NULL) {
        cl += strlen("\r\nContent-Length:");
        while (*cl == ' ' || *cl == '\t')
            cl++;

        char *endptr;
        long value = strtol(cl, &endptr, 10);

        if (endptr == cl || value < 0 ||
            value > BUFFER_SIZE - header_end - 1) {
            send_response(client, 413, "Content Too Large",
                          "{\"error\":\"Request body too large\"}");
            return;
        }

        content_length = (int)value;
    }

    while (used < header_end + content_length &&
           used < BUFFER_SIZE - 1) {
        int n = recv(
            client, buffer + used,
            BUFFER_SIZE - 1 - used, 0
        );

        if (n <= 0)
            return;

        used += n;
    }

    if (used < header_end + content_length) {
        send_response(client, 400, "Bad Request",
                      "{\"error\":\"Incomplete request body\"}");
        return;
    }

    printf("%s %s\n", method, path);

    /* Route the request. */
    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/api/hello") == 0) {

        send_response(client, 200, "OK",
                      "{\"message\":\"Hello from C on Windows!\"}");

    } else if (strcmp(method, "GET") == 0 &&
               strcmp(path, "/api/users") == 0) {

        send_response(client, 200, "OK",
                      "[{\"id\":1,\"name\":\"Alice\"},"
                      "{\"id\":2,\"name\":\"Bob\"}]");

    } else if (strcmp(method, "POST") == 0 &&
               strcmp(path, "/api/users") == 0) {

        /* Demo only: this does not parse or store the JSON body. */
        send_response(client, 201, "Created",
                      "{\"message\":\"User endpoint reached\"}");

    } else {
        send_response(client, 404, "Not Found",
                      "{\"error\":\"Route not found\"}");
    }
}

int main(void)
{
    WSADATA wsa;
    SOCKET server;
    struct sockaddr_in addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server, (struct sockaddr *)&addr,
             sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
        closesocket(server);
        WSACleanup();
        return 1;
    }

    printf("REST API running at http://localhost:%d\n", PORT);
    printf("Press Ctrl+C to stop.\n");

    for (;;) {
        SOCKET client = accept(server, NULL, NULL);

        if (client == INVALID_SOCKET) {
            fprintf(stderr, "accept failed: %d\n",
                    WSAGetLastError());
            continue;
        }

        handle_request(client);
        shutdown(client, SD_SEND);
        closesocket(client);
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
