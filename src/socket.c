#include "crequests/socket.h"
#include <ctinylogger/ctinylogger.h>

#include <string.h>

bool creq_socket_platform_init(void) {
#ifdef _WIN32
    WSADATA wsa_data;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (err != 0) {
        log_error("WSAStartup falhou com o código %d", err);
        return false;
    }
#endif
    /* POSIX (Linux/Android/Termux): sockets já funcionam sem nenhuma
     * inicialização de subsistema. */
    return true;
}

void creq_socket_platform_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

creq_socket_t creq_socket_connect(const char *host, const char *port) {
    creq_socket_t sockfd = CREQ_INVALID_SOCKET;
    struct addrinfo *addresses = NULL;

    /* AF_UNSPEC deixa o resolvedor escolher IPv4 ou IPv6, o que quer
     * que 'host' tenha; tentamos cada endereço retornado em ordem até
     * um aceitar a conexão. */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int error = getaddrinfo(host, port, &hints, &addresses);
    if (error != 0) {
#ifdef _WIN32
        log_error("Resolução de endereço falhou: host=%s port=%s (código %d)", host, port, error);
#else
        log_error("Resolução de endereço falhou: host=%s port=%s (%s)", host, port, gai_strerror(error));
#endif
        return CREQ_INVALID_SOCKET;
    }

    for (struct addrinfo *addr = addresses; addr; addr = addr->ai_next) {
        sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sockfd == CREQ_INVALID_SOCKET) {
            continue; /* tenta o próximo endereço */
        }
        if (connect(sockfd, addr->ai_addr, (int)addr->ai_addrlen) == 0) {
            break; /* conectado com sucesso */
        }
        creq_socket_close(sockfd);
        sockfd = CREQ_INVALID_SOCKET;
    }

    freeaddrinfo(addresses);

    if (sockfd == CREQ_INVALID_SOCKET) {
        log_error("Não foi possível conectar a host=%s port=%s", host, port);
    }
    return sockfd;
}

bool creq_socket_send_all(creq_socket_t sockfd, const void *data, size_t length) {
    const char *cursor = (const char *)data;
    size_t remaining = length;

    while (remaining > 0) {
        /* send() usa 'int' para o tamanho no Winsock; em POSIX é
         * size_t, mas o cast para int é seguro aqui pois nunca
         * enviamos mais que INT_MAX de uma vez (ver o 'chunk' abaixo). */
        int chunk = (remaining > (size_t)0x7FFFFFFF) ? 0x7FFFFFFF : (int)remaining;
        int sent = send(sockfd, cursor, chunk, 0);
        if (sent <= 0) {
            log_error("Falha ao enviar dados pelo socket (retorno %d)", sent);
            return false;
        }
        cursor += sent;
        remaining -= (size_t)sent;
    }
    return true;
}

int creq_socket_recv(creq_socket_t sockfd, unsigned char *buf, size_t buf_size) {
    int size = (buf_size > (size_t)0x7FFFFFFF) ? 0x7FFFFFFF : (int)buf_size;
    return recv(sockfd, (char *)buf, size, 0);
}
