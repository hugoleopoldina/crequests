# crequests (EM FASE DE TESTES)

Cliente HTTP/HTTPS mínimo para C, multiplataforma (**Linux**,
**Windows 7/10/11** e **Android/Termux**), com parser incremental de
respostas (máquina de estados), sessões com cookies persistentes e
manipulação de headers/cookies/parâmetros de query via cJSON.

```c
#include <crequests/crequests.h>

int main(void) {
    creq_global_init();

    CReqResponse *r = creq_get(NULL, "https://example.com/");
    if (r) {
        printf("%d %s -- %zu bytes\n", r->status_code, r->reason_phrase, r->body_length);
        creq_response_free(r);
    }

    creq_global_cleanup();
    return 0;
}
```

## Recursos

- **GET, POST, PUT, PATCH, DELETE, HEAD** prontos (`client.h`), além de
  uma função "mestre" (`creq_perform`) que executa qualquer
  `CReqRequest` já montada — incluindo verbos customizados
- **Parser de resposta incremental**, implementado como máquina de
  estados (`parser.h`): analisa a status-line, os headers e então
  decide como o corpo está delimitado —
  **`Content-Length`**, **`Transfer-Encoding: chunked`**, **sem corpo**
  (HEAD, 204, 304, 1xx) ou **até a conexão fechar** (HTTP/1.0 e
  similares) — processando os bytes conforme chegam da rede, por
  menores que sejam os pedaços
- **Sessões** (`CReqSession`) que persistem cookies entre requisições,
  como um navegador faz
- **Headers, cookies e parâmetros de query** com CRUD dedicado
  (`fields.h`), usando cJSON por baixo; parâmetros de query são
  percent-encoded automaticamente ao montar a requisição
- **`CReqResponse`** bem definida e reutilizável: status, versão,
  motivo, headers, cookies (extraídos de todos os `Set-Cookie`) e o
  corpo em bytes
- Corpo JSON de um clique: `creq_request_set_json_body()` /
  `creq_post_json()` serializam um `cJSON*` e já definem
  `Content-Type`/`Content-Length`
- TLS via OpenSSL (multiplataforma por conta própria); HTTP em texto
  plano também suportado
- Zero pool de conexões: cada requisição abre e fecha sua própria
  conexão (`Connection: close`) — simples e previsível, ao custo de um
  handshake TCP/TLS por chamada (ver "Limitações" abaixo)

## Instalação / build

Requer **CMake ≥ 3.15**, um compilador C11 e **OpenSSL** instalado no
sistema (headers + lib de desenvolvimento). cJSON e ctinylogger são
baixados automaticamente via `FetchContent` — não precisa instalá-los
à parte.

```bash
git clone <url-do-repositorio> crequests
cd crequests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Isso gera a lib estática (`build/libcrequests.a` no Linux/Android/Termux,
`build\crequests.lib` no Windows) e o executável de exemplo
`crequests_example` (desligue com `-DCREQUESTS_BUILD_EXAMPLE=OFF`).

Para instalar no sistema (headers + lib):

```bash
cmake --install build --prefix /caminho/de/instalacao
```

### OpenSSL por plataforma

- **Linux (Debian/Ubuntu)**: `sudo apt install libssl-dev`
- **Termux**: `pkg install openssl`
- **Windows**: instale via [vcpkg](https://github.com/microsoft/vcpkg)
  (`vcpkg install openssl`) e passe
  `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` ao
  configurar, ou use uma distribuição pré-compilada (ex: os binários do
  projeto [openssl-windows](https://github.com/openssl/openssl)) e
  aponte `OPENSSL_ROOT_DIR` para ela.

### Usando a lib em outro projeto (CMake)

Com `FetchContent`, do mesmo jeito que este projeto consome
ctinylogger e cJSON:

```cmake
include(FetchContent)
FetchContent_Declare(crequests
    GIT_REPOSITORY https://github.com/hugoleopoldina/crequests.git
    GIT_TAG main)
FetchContent_MakeAvailable(crequests)

target_link_libraries(seu_alvo PRIVATE crequests)
```

Ou, como subdiretório local: `add_subdirectory(caminho/para/crequests)`
+ `target_link_libraries(seu_alvo PRIVATE crequests)`.

## API

### Requisição simples (`client.h`)

```c
CReqResponse *creq_get(CReqSession *session, const char *url);
CReqResponse *creq_head(CReqSession *session, const char *url);
CReqResponse *creq_delete(CReqSession *session, const char *url);

CReqResponse *creq_post(CReqSession *session, const char *url,
                         const void *body, size_t body_length, const char *content_type);
CReqResponse *creq_put(CReqSession *session, const char *url,
                        const void *body, size_t body_length, const char *content_type);
CReqResponse *creq_patch(CReqSession *session, const char *url,
                          const void *body, size_t body_length, const char *content_type);

/* Assume posse de 'json' (cJSON_Delete interno) */
CReqResponse *creq_post_json(CReqSession *session, const char *url, cJSON *json);
```

`session` pode ser `NULL` para uma requisição avulsa (sem persistir
cookies).

### Requisição customizada: headers, cookies, parâmetros de query

```c
CReqSession *session = creq_session_create();
CReqRequest *req = creq_request_create(CREQ_METHOD_GET, "https://api.exemplo.com/busca");

creq_header_set(req, "Authorization", "Bearer token123");
creq_cookie_set(req, "session", "abc123");
creq_query_set(req, "termo", "café & pão");   /* percent-encoded automaticamente */
creq_query_set(req, "pagina", "2");

CReqResponse *resp = creq_perform(session, req); /* função "mestre" */

creq_request_free(req);
/* ... use resp ... */
creq_response_free(resp);
creq_session_free(session);
```

Funções análogas existem para leitura/remoção
(`creq_header_get/remove`, `creq_cookie_get/remove`,
`creq_query_get/remove`) — ver `include/crequests/fields.h`.

### Corpo da requisição

```c
/* bytes crus (ex: form-urlencoded, binário) */
bool creq_request_set_body(CReqRequest *request, const void *data, size_t length, bool copy);

/* serializa e assume posse de 'json'; define Content-Type/Content-Length */
bool creq_request_set_json_body(CReqRequest *request, cJSON *json);
```

### Resposta (`response.h`)

```c
typedef struct CReqResponse {
    char http_version[16];
    int  status_code;
    char reason_phrase[64];
    cJSON *headers;   /* {"nome": "valor", ...} -- exceto Set-Cookie */
    cJSON *cookies;   /* {"nome": "valor", ...} -- extraídos de Set-Cookie */
    CReqBodyType body_type;
    unsigned char *body;
    size_t body_length;
} CReqResponse;

const char *creq_response_header_get(const CReqResponse *response, const char *name); /* case-insensitive */
const char *creq_response_cookie_get(const CReqResponse *response, const char *name);
```

### Sessão (`session.h`)

```c
CReqSession *session = creq_session_create();
creq_session_set_default_header(session, "Authorization", "Bearer token123"); /* aplicado a toda requisição da sessão */

/* ... várias chamadas com 'session' ... cookies recebidos via
   Set-Cookie são automaticamente reaplicados nas próximas ... */

creq_session_clear_cookies(session); /* ex: simular logout */
creq_session_free(session);
```

### Persistir Sessão
```c
CReqSession *session = creq_session_create();
CReqRequest *req = creq_request_create(CREQ_METHOD_POST, "https://api.myapp.com/login");

/* Criar e definir o corpo da solicitação em JSON */
cJSON* body = cJSON_CreateObject();
cJSON_AddStringToObject(body, "username", "myusername");
cJSON_AddStringToObject(body, "password", "mypassword");

/* Essa chamada libera o objeto body
   e define o header `Content-Type` para `application/json` */
creq_request_set_json_body(req, body);

CReqResponse* resp = creq_perform(session, req);

/* Cookies definidos em session
   permitindo continuar a sessão em outras chamadas creq_perform(...) */

creq_session_free(session);
creq_response_free(resp);
creq_request_free(req);
/* 
```

### Parser (`parser.h`)

Uso normal (via `creq_perform`) não exige tocar no parser diretamente,
mas ele é exposto para quem quiser um cliente de transporte próprio
(ex: outra biblioteca de sockets):

```c
CReqResponse *resp = creq_response_create();
CReqParser parser;
creq_parser_init(&parser, resp, is_head_request);

CReqParseStatus status;
do {
    ssize_t n = /* leia bytes da sua conexão */;
    if (n > 0) {
        status = creq_parser_feed(&parser, buf, (size_t)n);
    } else {
        status = creq_parser_finish(&parser); /* conexão fechada (EOF) */
        break;
    }
} while (status == CREQ_PARSE_NEED_MORE);

creq_parser_destroy(&parser);
/* status == CREQ_PARSE_COMPLETE -> 'resp' está pronto para uso */
```

## Organização do projeto

```
crequests/
├── include/crequests/
│   ├── crequests.h      # header único que inclui toda a API pública
│   ├── types.h          # CReqMethod, CReqBodyType
│   ├── url.h            # CReqUrl + creq_url_parse/free
│   ├── request.h        # CReqRequest + create/free/set_body/set_json_body
│   ├── fields.h          # CRUD de headers/cookies/query params
│   ├── response.h       # CReqResponse + create/free/header_get/cookie_get
│   ├── session.h        # CReqSession (cookie jar reutilizável)
│   ├── parser.h         # máquina de estados incremental da resposta
│   ├── socket.h         # abstração de socket TCP (POSIX/Winsock)
│   └── tls.h            # wrapper OpenSSL
├── src/
│   ├── request_builder.h/.c  # monta os bytes crus da requisição (privado)
│   ├── internal.h/.c         # buffer de bytes + dicionário cJSON (privado)
│   └── *.c                   # implementação de cada header acima
├── examples/example.c
├── CMakeLists.txt
└── README.md
```

Sockets e TLS ficam em arquivos únicos com `#ifdef _WIN32` em vez de
pastas por plataforma: as diferenças entre Winsock e sockets BSD (ou
entre a mesma OpenSSL em cada SO) são pequenas o bastante para isso —
diferente, por exemplo, da API de console colorido do ctinylogger, que
difere o bastante entre Linux e Windows para justificar arquivos
próprios por plataforma.

## Limitações conhecidas

Este é um cliente propositalmente mínimo. Não implementa (ainda):

- Redirecionamentos automáticos (3xx) — o chamador decide o que fazer
  com a resposta e refaz a requisição manualmente se quiser seguir um
  `Location`
- Keep-alive / pipelining / pool de conexões — cada requisição usa sua
  própria conexão TCP/TLS, sempre com `Connection: close`
- Proxies HTTP/HTTPS
- HTTP/2 ou HTTP/3
- Compressão de corpo (`Content-Encoding: gzip`, etc.) — o corpo é
  entregue exatamente como veio pela rede
- Verificação de certificado customizada (usa a configuração padrão do
  OpenSSL) — para produção, considere carregar sua própria cadeia de
  CAs confiáveis via `SSL_CTX` (não exposto atualmente por `tls.h`)

Todas são extensões razoáveis para adicionar depois sem quebrar a API
pública atual.

## Licença

MIT — sinta-se livre para usar, modificar e distribuir.
