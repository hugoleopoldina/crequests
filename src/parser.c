/*
 * Implementação do parser incremental de respostas HTTP/1.x. Ver o
 * comentário em include/crequests/parser.h para uma visão geral dos
 * estados; aqui vão os detalhes de cada transição.
 */
#include "crequests/parser.h"
#include "internal.h"
#include <ctinylogger/ctinylogger.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Limite de tamanho para uma única linha (status-line, header ou linha
 * de tamanho de chunk) enquanto ainda não encontramos o CRLF final.
 * Sem isso, um servidor malicioso ou com bug poderia mandar uma
 * "linha" infinita (nunca fecha com \r\n) e nos forçar a acumular
 * memória sem limite antes de desistir. 16KB é generoso para
 * qualquer header realista. */
#define CREQ_MAX_LINE_LENGTH (16u * 1024u)

typedef enum {
    LINE_OK,
    LINE_NEED_MORE,
    LINE_ERROR
} LineStatus;

/* --------------------------------------------------------------------
 * Leitura de linhas (terminadas em CRLF) do buffer de entrada
 * ------------------------------------------------------------------ */

static const unsigned char *find_crlf(const unsigned char *buf, size_t len) {
    if (len < 2) {
        return NULL;
    }
    /* Busca ingênua, mas linhas HTTP são curtas (headers, status-line,
     * tamanhos de chunk) — não vale a pena um algoritmo mais esperto
     * aqui (tipo Boyer-Moore) para um padrão de 2 bytes. */
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            return buf + i;
        }
    }
    return NULL;
}

/* Extrai a próxima linha completa (sem o CRLF) a partir de
 * parser->cursor, avançando o cursor para depois do CRLF em caso de
 * sucesso. Retorna LINE_NEED_MORE se ainda não houver um CRLF completo
 * disponível (chamador deve esperar por mais dados), ou LINE_ERROR se
 * a linha excedeu CREQ_MAX_LINE_LENGTH sem terminar. */
static LineStatus next_line(CReqParser *parser, const unsigned char **line, size_t *line_len) {
    size_t available = parser->in_len - parser->cursor;
    const unsigned char *start = parser->in_buf + parser->cursor;
    const unsigned char *crlf = find_crlf(start, available);

    if (!crlf) {
        if (available > CREQ_MAX_LINE_LENGTH) {
            log_error("Linha HTTP excede o limite de %u bytes sem CRLF -- descartando conexão", CREQ_MAX_LINE_LENGTH);
            return LINE_ERROR;
        }
        return LINE_NEED_MORE;
    }

    *line = start;
    *line_len = (size_t)(crlf - start);
    parser->cursor += *line_len + 2; /* +2 para pular o \r\n em si */
    return LINE_OK;
}

/* Descarta do buffer de entrada os bytes já processados (antes de
 * parser->cursor), deslocando o restante para o início. Sem isso, uma
 * resposta grande faria o buffer crescer indefinidamente -- aqui é
 * onde a memória de bytes já "consumidos" é devolvida. Deve ser
 * chamado antes de qualquer retorno de creq_parser_feed(), para que a
 * próxima chamada comece com o mínimo de dados acumulados. */
static void compact_input(CReqParser *parser) {
    if (parser->cursor == 0) {
        return;
    }
    memmove(parser->in_buf, parser->in_buf + parser->cursor, parser->in_len - parser->cursor);
    parser->in_len -= parser->cursor;
    parser->cursor = 0;
}

/* --------------------------------------------------------------------
 * status-line: "HTTP/{versão} {código} {motivo}"
 * ------------------------------------------------------------------ */

static bool parse_status_line(CReqParser *parser, const unsigned char *line, size_t line_len) {
    CReqResponse *r = parser->response;
    const unsigned char *end = line + line_len;
    const unsigned char *p = line;

    if (line_len < 5 || memcmp(p, "HTTP/", 5) != 0) {
        log_error("Status-line não começa com 'HTTP/'");
        return false;
    }
    p += 5;

    /* versão: tudo até o próximo espaço */
    const unsigned char *version_start = p;
    while (p < end && *p != ' ') {
        p++;
    }
    size_t version_len = (size_t)(p - version_start);
    if (version_len == 0 || version_len >= sizeof(r->http_version)) {
        log_error("Versão HTTP ausente ou grande demais na status-line");
        return false;
    }
    memcpy(r->http_version, version_start, version_len);
    r->http_version[version_len] = '\0';

    if (p >= end) {
        log_error("Status-line sem código de status");
        return false;
    }
    p++; /* pula o espaço */

    /* código de status: 3 dígitos (mas aceitamos variações levemente
     * fora do padrão, contanto que sejam dígitos, por tolerância) */
    int code = 0, digits = 0;
    while (p < end && isdigit((unsigned char)*p)) {
        code = code * 10 + (*p - '0');
        p++;
        digits++;
    }
    if (digits == 0) {
        log_error("Código de status inválido na status-line");
        return false;
    }
    r->status_code = code;

    if (p < end && *p == ' ') {
        p++; /* pula o espaço antes da reason phrase */
    }

    size_t reason_len = (size_t)(end - p);
    if (reason_len >= sizeof(r->reason_phrase)) {
        reason_len = sizeof(r->reason_phrase) - 1; /* trunca, não é fatal */
    }
    memcpy(r->reason_phrase, p, reason_len);
    r->reason_phrase[reason_len] = '\0';

    return true;
}

/* --------------------------------------------------------------------
 * Linhas de header: "Nome: Valor"
 * ------------------------------------------------------------------ */

static bool split_header_line(const unsigned char *line, size_t line_len,
                               const unsigned char **name, size_t *name_len,
                               const unsigned char **value, size_t *value_len) {
    const unsigned char *colon = (const unsigned char *)memchr(line, ':', line_len);
    if (!colon) {
        return false;
    }

    *name = line;
    *name_len = (size_t)(colon - line);
    if (*name_len == 0) {
        return false;
    }

    const unsigned char *v = colon + 1;
    const unsigned char *end = line + line_len;

    /* RFC 7230 permite espaços/tabs opcionais (OWS) entre ':' e o
     * valor, e no final do valor -- removemos ambos aqui para que o
     * valor guardado seja sempre "limpo". */
    while (v < end && (*v == ' ' || *v == '\t')) {
        v++;
    }
    while (end > v && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }

    *value = v;
    *value_len = (size_t)(end - v);
    return true;
}

/* Extrai "nome=valor" de um header Set-Cookie bruto (ignorando
 * atributos como Path=, Expires=, HttpOnly, etc. depois do primeiro
 * ';') e o guarda em response->cookies. */
static bool store_set_cookie(CReqResponse *response, const char *raw_cookie) {
    const char *semi = strchr(raw_cookie, ';');
    size_t pair_len = semi ? (size_t)(semi - raw_cookie) : strlen(raw_cookie);

    const char *eq = (const char *)memchr(raw_cookie, '=', pair_len);
    if (!eq) {
        log_warning("Set-Cookie sem '=' ignorado: %s", raw_cookie);
        return true; /* não fatal: só ignora este cookie específico */
    }

    size_t name_len = (size_t)(eq - raw_cookie);
    size_t value_len = pair_len - name_len - 1;

    char *name = (char *)malloc(name_len + 1);
    char *value = (char *)malloc(value_len + 1);
    bool ok = false;

    if (name && value) {
        memcpy(name, raw_cookie, name_len);
        name[name_len] = '\0';
        memcpy(value, eq + 1, value_len);
        value[value_len] = '\0';
        ok = creq_dict_set(&response->cookies, name, value, false);
    }

    free(name);
    free(value);
    return ok;
}

static bool process_header_line(CReqParser *parser, const unsigned char *line, size_t line_len) {
    const unsigned char *name_ptr, *value_ptr;
    size_t name_len, value_len;

    if (!split_header_line(line, line_len, &name_ptr, &name_len, &value_ptr, &value_len)) {
        /* Linha sem ':' -- não é um header válido. Alguns servidores
         * antigos usam "dobra" de linha (obsoleta desde a RFC 7230);
         * não suportamos isso, mas em vez de abortar a resposta
         * inteira só ignoramos a linha e seguimos, o que é mais
         * tolerante e não perde headers válidos que viriam depois. */
        log_warning("Linha de cabeçalho sem ':' ignorada");
        return true;
    }

    /* Nomes de header são sempre curtos na prática; um buffer de pilha
     * de 256 bytes evita um malloc para o caso comum. */
    char name_buf[256];
    if (name_len >= sizeof(name_buf)) {
        name_len = sizeof(name_buf) - 1;
    }
    memcpy(name_buf, name_ptr, name_len);
    name_buf[name_len] = '\0';

    char *value_buf = (char *)malloc(value_len + 1);
    if (!value_buf) {
        log_error("Falha de memória processando header '%s'", name_buf);
        return false;
    }
    memcpy(value_buf, value_ptr, value_len);
    value_buf[value_len] = '\0';

    bool ok;
    if (creq_strcasecmp(name_buf, "Set-Cookie") == 0) {
        ok = store_set_cookie(parser->response, value_buf);
    } else {
        ok = creq_dict_set(&parser->response->headers, name_buf, value_buf, true);
    }

    free(value_buf);
    return ok;
}

/* --------------------------------------------------------------------
 * Decisão do tipo de corpo, ao final dos headers (RFC 7230 §3.3.3)
 * ------------------------------------------------------------------ */

static bool is_bodyless_status(int status_code) {
    /* 1xx (informational), 204 (No Content) e 304 (Not Modified) nunca
     * têm corpo, independente do que Content-Length/Transfer-Encoding
     * digam. */
    return (status_code >= 100 && status_code < 200) || status_code == 204 || status_code == 304;
}

static bool decide_body_type(CReqParser *parser) {
    CReqResponse *r = parser->response;

    if (parser->is_head_request || is_bodyless_status(r->status_code)) {
        r->body_type = CREQ_BODY_NONE;
        parser->state = CREQ_PSTATE_DONE;
        return true;
    }

    const char *transfer_encoding = creq_response_header_get(r, "Transfer-Encoding");
    if (transfer_encoding && creq_strcasecmp(transfer_encoding, "chunked") == 0) {
        r->body_type = CREQ_BODY_CHUNKED;
        parser->state = CREQ_PSTATE_BODY_CHUNKED_SIZE;
        return true;
    }

    const char *content_length_str = creq_response_header_get(r, "Content-Length");
    if (content_length_str) {
        char *endptr = NULL;
        unsigned long long parsed = strtoull(content_length_str, &endptr, 10);

        if (!endptr || *endptr != '\0' || endptr == content_length_str) {
            log_error("Content-Length inválido: '%s'", content_length_str);
            return false;
        }

        r->body_type = CREQ_BODY_CONTENT_LENGTH;
        parser->content_length = (size_t)parsed;
        parser->state = (parser->content_length == 0) ? CREQ_PSTATE_DONE : CREQ_PSTATE_BODY_CONTENT_LENGTH;
        return true;
    }

    /* Nem chunked, nem Content-Length: pela RFC 7230 §3.3.3 item 7,
     * o corpo se estende até a conexão ser fechada pelo servidor.
     * Comum em respostas HTTP/1.0 ou servidores que preferem fechar a
     * conexão a calcular o tamanho do corpo antecipadamente. */
    r->body_type = CREQ_BODY_UNTIL_CLOSE;
    parser->state = CREQ_PSTATE_BODY_UNTIL_CLOSE;
    return true;
}

/* --------------------------------------------------------------------
 * Corpo: acumula bytes em response->body com crescimento amortizado
 * ------------------------------------------------------------------ */

static bool append_body(CReqParser *parser, const unsigned char *data, size_t len) {
    CReqBuf buf;
    buf.data = parser->response->body;
    buf.len = parser->response->body_length;
    buf.cap = parser->body_alloc;

    if (!creq_buf_append(&buf, data, len)) {
        return false;
    }

    parser->response->body = buf.data;
    parser->response->body_length = buf.len;
    parser->body_alloc = buf.cap;
    return true;
}

/* Analisa a linha hexadecimal de tamanho de um chunk, ex: "1a\r\n" ou
 * "1a;alguma-extensao\r\n" (extensões de chunk são raras na prática e
 * simplesmente ignoradas aqui, como a maioria dos clientes faz). */
static bool parse_chunk_size_line(const unsigned char *line, size_t line_len, size_t *out_size) {
    /* corta na primeira ';' (extensão de chunk), se houver */
    size_t hex_len = 0;
    while (hex_len < line_len && line[hex_len] != ';') {
        hex_len++;
    }
    if (hex_len == 0 || hex_len >= 32) {
        return false;
    }

    char hex_buf[32];
    memcpy(hex_buf, line, hex_len);
    hex_buf[hex_len] = '\0';

    char *endptr = NULL;
    unsigned long long size = strtoull(hex_buf, &endptr, 16);
    if (!endptr || *endptr != '\0' || endptr == hex_buf) {
        return false;
    }

    *out_size = (size_t)size;
    return true;
}

/* --------------------------------------------------------------------
 * API pública
 * ------------------------------------------------------------------ */

void creq_parser_init(CReqParser *parser, CReqResponse *response, bool is_head_request) {
    memset(parser, 0, sizeof(*parser));
    parser->response = response;
    parser->is_head_request = is_head_request;
    parser->state = CREQ_PSTATE_STATUS_LINE;
}

void creq_parser_destroy(CReqParser *parser) {
    free(parser->in_buf);
    parser->in_buf = NULL;
    parser->in_len = 0;
    parser->in_alloc = 0;
    parser->cursor = 0;
}

CReqParseStatus creq_parser_feed(CReqParser *parser, const unsigned char *data, size_t len) {
    if (parser->state == CREQ_PSTATE_DONE) {
        return CREQ_PARSE_COMPLETE; /* chamadas extras após concluir são idempotentes */
    }

    {
        CReqBuf buf;
        buf.data = parser->in_buf;
        buf.len = parser->in_len;
        buf.cap = parser->in_alloc;

        if (!creq_buf_append(&buf, data, len)) {
            log_error("creq_parser_feed: falha de memória acumulando %zu bytes", len);
            return CREQ_PARSE_ERROR;
        }

        parser->in_buf = buf.data;
        parser->in_len = buf.len;
        parser->in_alloc = buf.cap;
    }

    for (;;) {
        const unsigned char *line;
        size_t line_len;
        LineStatus ls;

        switch (parser->state) {

        case CREQ_PSTATE_STATUS_LINE:
            ls = next_line(parser, &line, &line_len);
            if (ls == LINE_NEED_MORE) { compact_input(parser); return CREQ_PARSE_NEED_MORE; }
            if (ls == LINE_ERROR) { return CREQ_PARSE_ERROR; }

            if (!parse_status_line(parser, line, line_len)) {
                return CREQ_PARSE_ERROR;
            }
            parser->state = CREQ_PSTATE_HEADERS;
            continue;

        case CREQ_PSTATE_HEADERS:
            ls = next_line(parser, &line, &line_len);
            if (ls == LINE_NEED_MORE) { compact_input(parser); return CREQ_PARSE_NEED_MORE; }
            if (ls == LINE_ERROR) { return CREQ_PARSE_ERROR; }

            if (line_len == 0) {
                /* linha vazia: fim dos headers -- decide o tipo de
                 * corpo e transiciona para o estado correspondente */
                if (!decide_body_type(parser)) {
                    return CREQ_PARSE_ERROR;
                }
                if (parser->state == CREQ_PSTATE_DONE) {
                    compact_input(parser);
                    return CREQ_PARSE_COMPLETE;
                }
                continue;
            }

            if (!process_header_line(parser, line, line_len)) {
                return CREQ_PARSE_ERROR;
            }
            continue; /* pode haver mais headers completos já no buffer */

        case CREQ_PSTATE_BODY_CONTENT_LENGTH: {
            size_t available = parser->in_len - parser->cursor;
            size_t remaining_needed = parser->content_length - parser->response->body_length;
            size_t take = (available < remaining_needed) ? available : remaining_needed;

            if (take > 0) {
                if (!append_body(parser, parser->in_buf + parser->cursor, take)) {
                    return CREQ_PARSE_ERROR;
                }
                parser->cursor += take;
            }

            if (parser->response->body_length >= parser->content_length) {
                parser->state = CREQ_PSTATE_DONE;
                compact_input(parser);
                return CREQ_PARSE_COMPLETE;
            }

            compact_input(parser);
            return CREQ_PARSE_NEED_MORE;
        }

        case CREQ_PSTATE_BODY_CHUNKED_SIZE:
            ls = next_line(parser, &line, &line_len);
            if (ls == LINE_NEED_MORE) { compact_input(parser); return CREQ_PARSE_NEED_MORE; }
            if (ls == LINE_ERROR) { return CREQ_PARSE_ERROR; }

            {
                size_t chunk_size = 0;
                if (!parse_chunk_size_line(line, line_len, &chunk_size)) {
                    log_error("Linha de tamanho de chunk inválida");
                    return CREQ_PARSE_ERROR;
                }

                if (chunk_size == 0) {
                    /* chunk de tamanho 0 marca o fim do corpo; pode
                     * ainda vir "trailers" (headers extras) antes da
                     * linha vazia final */
                    parser->state = CREQ_PSTATE_BODY_CHUNKED_TRAILER;
                } else {
                    parser->chunk_remaining = chunk_size;
                    parser->state = CREQ_PSTATE_BODY_CHUNKED_DATA;
                }
            }
            continue;

        case CREQ_PSTATE_BODY_CHUNKED_DATA: {
            size_t available = parser->in_len - parser->cursor;
            size_t take = (available < parser->chunk_remaining) ? available : parser->chunk_remaining;

            if (take > 0) {
                if (!append_body(parser, parser->in_buf + parser->cursor, take)) {
                    return CREQ_PARSE_ERROR;
                }
                parser->cursor += take;
                parser->chunk_remaining -= take;
            }

            if (parser->chunk_remaining > 0) {
                compact_input(parser);
                return CREQ_PARSE_NEED_MORE;
            }

            /* O chunk inteiro foi copiado; falta só o CRLF que
             * obrigatoriamente encerra os dados do chunk antes do
             * próximo tamanho (RFC 7230 §4.1). */
            if (parser->in_len - parser->cursor < 2) {
                compact_input(parser);
                return CREQ_PARSE_NEED_MORE;
            }
            if (parser->in_buf[parser->cursor] != '\r' || parser->in_buf[parser->cursor + 1] != '\n') {
                log_error("CRLF final do chunk ausente/corrompido");
                return CREQ_PARSE_ERROR;
            }
            parser->cursor += 2;
            parser->state = CREQ_PSTATE_BODY_CHUNKED_SIZE;
            continue;
        }

        case CREQ_PSTATE_BODY_CHUNKED_TRAILER:
            ls = next_line(parser, &line, &line_len);
            if (ls == LINE_NEED_MORE) { compact_input(parser); return CREQ_PARSE_NEED_MORE; }
            if (ls == LINE_ERROR) { return CREQ_PARSE_ERROR; }

            if (line_len == 0) {
                parser->state = CREQ_PSTATE_DONE;
                compact_input(parser);
                return CREQ_PARSE_COMPLETE;
            }
            /* trailers são raros na prática; tratamos como headers
             * normais (se algum servidor mandar um Set-Cookie como
             * trailer, ainda assim é reconhecido corretamente) */
            if (!process_header_line(parser, line, line_len)) {
                return CREQ_PARSE_ERROR;
            }
            continue;

        case CREQ_PSTATE_BODY_UNTIL_CLOSE: {
            size_t available = parser->in_len - parser->cursor;
            if (available > 0) {
                if (!append_body(parser, parser->in_buf + parser->cursor, available)) {
                    return CREQ_PARSE_ERROR;
                }
                parser->cursor += available;
            }
            /* Só sabemos que este corpo terminou quando a conexão
             * fechar -- ver creq_parser_finish(). Até lá, sempre
             * "precisa de mais". */
            compact_input(parser);
            return CREQ_PARSE_NEED_MORE;
        }

        case CREQ_PSTATE_DONE:
            return CREQ_PARSE_COMPLETE;
        }
    }
}

CReqParseStatus creq_parser_finish(CReqParser *parser) {
    if (parser->state == CREQ_PSTATE_BODY_UNTIL_CLOSE) {
        /* Este é o único estado em que EOF é o final ESPERADO da
         * resposta (ver decide_body_type()). */
        parser->state = CREQ_PSTATE_DONE;
        return CREQ_PARSE_COMPLETE;
    }

    if (parser->state == CREQ_PSTATE_DONE) {
        return CREQ_PARSE_COMPLETE;
    }

    /* Qualquer outro estado significa que a conexão fechou antes da
     * resposta estar completa (ex: servidor prometeu Content-Length: N
     * mas fechou depois de enviar menos que isso) -- é um erro. */
    log_error("Conexão fechada com a resposta HTTP incompleta (estado=%d)", (int)parser->state);
    return CREQ_PARSE_ERROR;
}
