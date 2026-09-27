/*
 * Parser incremental de respostas HTTP/1.x, implementado como uma
 * máquina de estados. "Incremental" quer dizer: os bytes chegam aos
 * pedaços (do socket/TLS), possivelmente cortando uma linha ou um
 * chunk ao meio, e o parser precisa lembrar exatamente onde parou
 * entre uma chamada de creq_parser_feed() e a próxima.
 *
 * Fluxo de estados:
 *
 *   CREQ_PSTATE_STATUS_LINE
 *       -> analisa "HTTP/{versão} {código} {motivo}\r\n"
 *   CREQ_PSTATE_HEADERS
 *       -> analisa linhas "Nome: Valor\r\n" até uma linha vazia
 *       -> ao encontrar a linha vazia, decide o tipo de corpo
 *          (creq_body_type abaixo) e segue para o estado de corpo
 *          correspondente (ou direto para DONE se não houver corpo)
 *   CREQ_PSTATE_BODY_CONTENT_LENGTH
 *       -> copia exatamente Content-Length bytes para o corpo
 *   CREQ_PSTATE_BODY_CHUNKED_SIZE
 *       -> analisa a linha hexadecimal de tamanho de um chunk
 *   CREQ_PSTATE_BODY_CHUNKED_DATA
 *       -> copia exatamente 'chunk_remaining' bytes para o corpo,
 *          depois consome o CRLF final do chunk e volta para
 *          CREQ_PSTATE_BODY_CHUNKED_SIZE (tamanho 0 -> trailers -> DONE)
 *   CREQ_PSTATE_BODY_CHUNKED_TRAILER
 *       -> chunks terminam com "trailers" opcionais (mais linhas de
 *          header), seguidos de uma linha vazia -> DONE
 *   CREQ_PSTATE_BODY_UNTIL_CLOSE
 *       -> sem Content-Length nem chunked: todo byte recebido é corpo,
 *          até a conexão fechar (sinalizado por creq_parser_finish())
 *   CREQ_PSTATE_DONE
 *       -> resposta completa
 */
#ifndef CREQ_PARSER_H
#define CREQ_PARSER_H

#include <stddef.h>
#include <stdbool.h>

#include "crequests/response.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CREQ_PARSE_NEED_MORE, /* linha/dado incompleto: chame feed() de novo com mais bytes */
    CREQ_PARSE_COMPLETE,  /* resposta 100% analisada (chegou a CREQ_PSTATE_DONE) */
    CREQ_PARSE_ERROR      /* dado malformado; o parser não deve mais ser alimentado */
} CReqParseStatus;

typedef enum {
    CREQ_PSTATE_STATUS_LINE = 0,
    CREQ_PSTATE_HEADERS,
    CREQ_PSTATE_BODY_CONTENT_LENGTH,
    CREQ_PSTATE_BODY_CHUNKED_SIZE,
    CREQ_PSTATE_BODY_CHUNKED_DATA,
    CREQ_PSTATE_BODY_CHUNKED_TRAILER,
    CREQ_PSTATE_BODY_UNTIL_CLOSE,
    CREQ_PSTATE_DONE
} CReqParserState;

typedef struct CReqParser {
    /* Buffer de bytes ainda não processados. Cresce sob demanda (ver
     * src/parser.c) e é periodicamente compactado (bytes já
     * consumidos são descartados) para não crescer indefinidamente ao
     * longo de um download grande. */
    unsigned char *in_buf;
    size_t in_len;    /* bytes válidos em in_buf */
    size_t in_alloc;  /* capacidade alocada de in_buf */
    size_t cursor;    /* próximo byte não processado dentro de in_buf */

    CReqParserState state;

    /* Resposta sendo preenchida incrementalmente. Não é dona: quem
     * criou o parser (client.c) é quem cria/libera a CReqResponse. */
    CReqResponse *response;

    /* true se a requisição associada era HEAD: o corpo, mesmo que os
     * headers descrevam um (Content-Length, chunked), nunca é enviado
     * pelo servidor para respostas a HEAD (RFC 7230 §3.3.3), então o
     * parser deve ir direto para DONE após os headers. */
    bool is_head_request;

    /* Estado específico do corpo, calculado ao final dos headers.
     * Quantos bytes de corpo já foram copiados é sempre
     * response->body_length -- não duplicamos essa contagem aqui. */
    size_t content_length;     /* alvo, quando body_type == CONTENT_LENGTH */
    size_t body_alloc;         /* capacidade alocada de response->body (crescimento amortizado) */
    size_t chunk_remaining;    /* bytes restantes do chunk atual (modo chunked) */
} CReqParser;

/* Inicializa um parser recém-alocado (stack ou heap) para começar a
 * analisar uma nova resposta em 'response' (deve já existir, ver
 * creq_response_create()). 'is_head_request' vem do método da
 * requisição original. */
void creq_parser_init(CReqParser *parser, CReqResponse *response, bool is_head_request);

/* Libera os buffers internos do parser (NÃO libera 'response', que tem
 * vida própria e é responsabilidade do chamador). Seguro chamar mesmo
 * após um erro ou conclusão. */
void creq_parser_destroy(CReqParser *parser);

/* Alimenta o parser com mais 'len' bytes recebidos da rede (ex: saída
 * de SSL_read/recv). Pode ser chamado repetidamente conforme os dados
 * chegam. Retorna o novo status da análise. */
CReqParseStatus creq_parser_feed(CReqParser *parser, const unsigned char *data, size_t len);

/* Deve ser chamado quando a conexão for fechada pelo servidor (EOF).
 * Necessário para concluir corretamente um corpo do tipo
 * CREQ_BODY_UNTIL_CLOSE, que por definição não tem um fim conhecido
 * até que a conexão feche. Para os demais tipos de corpo, chamar isto
 * antes de CREQ_PARSE_COMPLETE indica uma conexão fechada
 * prematuramente e retorna CREQ_PARSE_ERROR. */
CReqParseStatus creq_parser_finish(CReqParser *parser);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_PARSER_H */
