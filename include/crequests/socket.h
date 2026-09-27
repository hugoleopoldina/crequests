/*
 * Abstração mínima de socket TCP cliente, multiplataforma. As
 * diferenças entre Winsock (Windows) e sockets BSD (Linux/Android/
 * Termux) são pequenas o bastante (nomes de tipo/função e
 * inicialização do subsistema) para ficarem resolvidas com #ifdef
 * neste único arquivo, em vez de uma pasta por plataforma — ao
 * contrário da camada de console da ctinylogger, cuja API de cores
 * difere o bastante entre SOs para justificar arquivos separados.
 */
#ifndef CREQ_SOCKET_H
#define CREQ_SOCKET_H

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET creq_socket_t;
#define CREQ_INVALID_SOCKET INVALID_SOCKET
#define creq_socket_close closesocket

#pragma comment(lib, "ws2_32.lib")

#else /* POSIX: Linux, Android/Termux */

/* Em modo estritamente C11/C17 (sem GNU extensions), a glibc só expõe
 * getaddrinfo/freeaddrinfo/struct addrinfo se alguma feature-test
 * macro POSIX estiver definida ANTES de qualquer include -- caso
 * contrário eles somem de <netdb.h> mesmo existindo no sistema. Isto
 * garante que crequests compile igual com -std=c11 puro ou com as
 * extensões GNU habilitadas. */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

typedef int creq_socket_t;
#define CREQ_INVALID_SOCKET (-1)
#define creq_socket_close close

#endif

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa o subsistema de sockets da plataforma. No Windows, chama
 * WSAStartup; no POSIX é um no-op (sockets já funcionam sem setup).
 * Chamado internamente por creq_global_init() — não precisa ser
 * chamado manualmente. */
bool creq_socket_platform_init(void);

/* Contraparte de creq_socket_platform_init() (WSACleanup no Windows). */
void creq_socket_platform_cleanup(void);

/* Resolve 'host'/'port' e conecta um socket TCP a ele, tentando cada
 * endereço retornado (IPv4/IPv6) até um conectar com sucesso. Retorna
 * CREQ_INVALID_SOCKET em caso de falha (erro já registrado via
 * log_error). */
creq_socket_t creq_socket_connect(const char *host, const char *port);

/* Envia 'length' bytes de 'data' pelo socket em texto plano (sem TLS),
 * repetindo o envio até que tudo seja escrito ou ocorra um erro.
 * Usado apenas para URLs http:// -- toda URL https:// passa por
 * tls.h. Retorna true em caso de sucesso. */
bool creq_socket_send_all(creq_socket_t sockfd, const void *data, size_t length);

/* Lê até 'buf_size' bytes do socket para 'buf' (sem TLS). Retorna a
 * quantidade lida (> 0), 0 se o peer fechou a conexão normalmente, ou
 * um valor negativo em caso de erro. */
int creq_socket_recv(creq_socket_t sockfd, unsigned char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_SOCKET_H */
