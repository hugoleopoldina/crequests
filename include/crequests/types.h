/*
 * crequests - cliente HTTP/HTTPS mínimo, multiplataforma.
 *
 * Tipos e enums compartilhados por toda a lib.
 */
#ifndef CREQ_TYPES_H
#define CREQ_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Métodos HTTP suportados diretamente pelos atalhos de client.h. Qualquer
 * outro verbo pode ser usado via creq_request_create() passando o método
 * como string em vez de CReqMethod (ver request.h). */
typedef enum {
    CREQ_METHOD_GET = 0,
    CREQ_METHOD_POST,
    CREQ_METHOD_PUT,
    CREQ_METHOD_PATCH,
    CREQ_METHOD_DELETE,
    CREQ_METHOD_HEAD,
    CREQ_METHOD_OPTIONS
} CReqMethod;

/* Como o corpo da resposta está delimitado. Determinado pelo parser (ver
 * parser.h) a partir dos cabeçalhos da resposta, e exposto aqui pois é
 * uma informação útil para quem consome CReqResponse (ex: saber se a
 * ausência de corpo era esperada). */
typedef enum {
    CREQ_BODY_NONE = 0,      /* sem corpo (HEAD, 204, 304, 1xx, etc.) */
    CREQ_BODY_CONTENT_LENGTH,/* corpo com tamanho fixo (header Content-Length) */
    CREQ_BODY_CHUNKED,       /* corpo em pedaços (header Transfer-Encoding: chunked) */
    CREQ_BODY_UNTIL_CLOSE    /* sem Content-Length nem chunked: vai até a conexão fechar */
} CReqBodyType;

/* Converte um CReqMethod para sua representação textual usada na
 * request-line HTTP (ex: CREQ_METHOD_GET -> "GET"). */
const char *creq_method_to_string(CReqMethod method);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_TYPES_H */
