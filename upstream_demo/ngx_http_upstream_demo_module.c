#include "ngx_core.h"
#include "ngx_config.h"
#include "ngx_http.h"

typedef struct ngx_http_upstream_demo_ctx_s {
    ngx_http_status_t status;
    ngx_str_t addr;
}ngx_http_upstream_demo_ctx_t;

typedef struct ngx_http_upstream_demo_loc_ctx_s {
    ngx_http_upstream_conf_t up_conf;
}ngx_http_upstream_demo_loc_ctx_t;

ngx_module_t ngx_http_upstream_demo_module;

static ngx_str_t  ngx_http_upstream_demo_hide_headers[] =
{
    ngx_string("Date"),
    ngx_string("Server"),
    ngx_string("X-Pad"),
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_string("X-Accel-Charset"),
    ngx_null_string
};

ngx_int_t ngx_http_upstream_demo_create_request_handler(ngx_http_request_t *r)
{
    ngx_buf_t* buf = NULL;
    ngx_str_t query_line = ngx_string("GET /api/ip?wd=%V HTTP/1.1\r\nHost: worldtimeapi.org\r\nConnection: close\r\n\r\n");
    ngx_int_t query_line_len = query_line.len + r->args.len - 2;

    buf = ngx_create_temp_buf(r->connection->pool, query_line_len);
    if(buf == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "create tmp buf fail");
        return NGX_ERROR;
    }
    buf->last = buf->pos + query_line_len;
    ngx_snprintf(buf->pos, query_line_len, (char*)query_line.data, &r->args);
    // 打印不出来，待解决；
    // ngx_log_debug2(NGX_LOG_DEBUG, r->connection->log, 0, "upstream demo request %*s", query_line_len, buf->start);

    r->upstream->request_bufs = ngx_alloc_chain_link(r->pool);
    if(r->upstream->request_bufs == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "alloc chain link fail");
        return NGX_ERROR;
    }

    r->upstream->request_bufs->buf = buf;
    r->upstream->request_bufs->next = NULL;

    /* 是否已经开始给 上游发送报文 */
    r->upstream->request_sent = 0;
    /* 是否已经转发上游header 给下游*/
    r->upstream->header_sent = 0;

    /* 用于存储解析http 头项的hash 值 */
    r->header_hash = 1;

    return NGX_OK;

}

static ngx_int_t ngx_http_upstream_demo_process_header_handler(ngx_http_request_t *r)
{
    ngx_int_t rc;
    ngx_table_elt_t* h;
    ngx_http_upstream_main_conf_t* umcf = NULL;
    ngx_http_upstream_header_t* uh = NULL;


    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    while(1) {
        rc = ngx_http_parse_header_line(r, &r->upstream->buffer, 1);
        if(rc == NGX_AGAIN) {
            return rc;
        }
        if(rc == NGX_HTTP_PARSE_HEADER_DONE) {
            break;
        }
         if(rc != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, rc, "parse http header fail, headers: %s, pos: %d", 
                r->upstream->buffer, r->upstream->buffer.pos);
            return rc;
        }

        // NGX_OK 
        h = ngx_list_push(&r->upstream->headers_in.headers);
        if(h == NULL) {
            return NGX_ERROR;
        }

        h->hash = r->header_hash;
        h->key.len = r->header_name_end - r->header_name_start;
        h->value.len = r->header_end - r->header_start;

        h->key.data = ngx_palloc(r->pool, h->key.len + 1 + h->value.len + 1 + h->key.len);
        if(h->key.data == NULL) {
            return NGX_ERROR;
        }
        h->value.data = h->key.data + h->key.len + 1;
        h->lowcase_key = h->key.data + h->key.len + 1 + h->value.len + 1;

        ngx_memcpy(h->key.data, r->header_name_start, h->key.len);
        h->key.data[h->key.len] = 0;
        ngx_memcpy(h->value.data, r->header_start, h->value.len);
        h->value.data[h->value.len] = 0;

        // r->lowcase_index 最长 32 字节
        if (h->key.len == r->lowcase_index)
        {
            ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);
        }
        else
        {
            ngx_strlow(h->lowcase_key, h->key.data, h->key.len);
        }

        // ngx_http_upstream_init_main_conf 函数中给 headers_in_hash 注册了一些特殊头文件处理
        // ngx_http_upstream_headers_in
        uh = ngx_hash_find(&umcf->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
        if(uh && uh->handler) {
            rc = uh->handler(r, h, uh->offset);
            if(rc != NGX_OK) {
                return rc;
            }
        }

        continue;
    }
    
    // 如果之前解析http头部时没有发现server和date头部，以下会
    //根据http协议添加这两个头部，这两个header 在 ngx_http_upstream_headers_in 中处理
    if(r->upstream->headers_in.server == NULL) {
        h = ngx_list_push(&r->upstream->headers_in.headers);
        if(h == NULL) {
            return NGX_ERROR;
        }

        h->hash = ngx_hash(ngx_hash(ngx_hash(ngx_hash(ngx_hash('s', 'e'), 'r'), 'v'), 'e'), 'r');
        ngx_str_set(&h->key, "Server");
        ngx_str_null(&h->value);
        h->lowcase_key = (u_char*) "server";
    }

    if(r->upstream->headers_in.date == NULL) {
        h = ngx_list_push(&r->upstream->headers_in.headers);
        if(h == NULL) {
            return NGX_ERROR;
        }

        h->hash = ngx_hash(ngx_hash(ngx_hash('d', 'a'), 't'), 'e');
        ngx_str_set(&h->key, "Date");
        ngx_str_null(&h->value);
        h->lowcase_key = (u_char*) "date";
    }

    return NGX_OK;
}

static ngx_int_t ngx_http_upstream_demo_process_status_line_handler(ngx_http_request_t *r)
{
    ngx_int_t rc;
    ngx_http_upstream_demo_ctx_t* up_demo_ctx = NULL;
    ngx_http_upstream_t* u = NULL;

    up_demo_ctx = ngx_http_get_module_ctx(r, ngx_http_upstream_demo_module);
    if(NULL == up_demo_ctx) {
        return NGX_ERROR;
    }

    u = r->upstream;
    /*分配出的内存，可能包含脏数据*/
    ngx_memzero(&up_demo_ctx->status, sizeof(ngx_http_status_t));
    ngx_str_null(&up_demo_ctx->addr);

    rc = ngx_http_parse_status_line(r, &r->upstream->buffer, &up_demo_ctx->status);
    if(rc == NGX_AGAIN) {
        return rc;
    }
    else if(rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream sent no valid HTTP/1.0 header");
        r->http_version = NGX_HTTP_VERSION_9;
        u->state->status = NGX_HTTP_OK;
        // 如果返回 NGX_ERROR，会返回 error page
        return NGX_ERROR;
    }

    if(u->state) {
        u->state->status = up_demo_ctx->status.code;
    }

    u->headers_in.status_n = up_demo_ctx->status.code;
    u->headers_in.status_line.len = up_demo_ctx->status.end - up_demo_ctx->status.start;
    u->headers_in.status_line.data = ngx_palloc(r->pool, u->headers_in.status_line.len);
    if(u->headers_in.status_line.data == NULL) {
        return NGX_ERROR;
    }
    ngx_memcpy(u->headers_in.status_line.data, up_demo_ctx->status.start, u->headers_in.status_line.len);

    u->process_header = ngx_http_upstream_demo_process_header_handler;

    return ngx_http_upstream_demo_process_header_handler(r);
}

static void ngx_http_upstream_demo_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "http upstream demo finalize request");
}

ngx_int_t ngx_http_upstream_demo_handler(ngx_http_request_t *r)
{
    int rc;
    ngx_http_upstream_demo_ctx_t* up_demo_ctx = NULL;
    ngx_http_upstream_demo_loc_ctx_t* up_demo_loc_ctx = NULL;
    ngx_http_upstream_t* u = NULL;
    struct sockaddr_in* sock_addr = NULL;
    struct hostent* hostent = NULL;
    char* host;

    // 首先建立http上下文结构体ngx_http_mytest_ctx_t
    up_demo_ctx = ngx_http_get_module_ctx(r, ngx_http_upstream_demo_module);
    if(up_demo_ctx == NULL) {
            up_demo_ctx = ngx_pnalloc(r->pool, sizeof(ngx_http_upstream_demo_ctx_t));
            if(up_demo_ctx == NULL) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "alloc upstream demo ctx mem fail");
                return NGX_ERROR;
            }
            ngx_http_set_ctx(r, up_demo_ctx, ngx_http_upstream_demo_module);
    }

    rc = ngx_http_upstream_create(r);
    if(rc != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, rc, "upstream demo module create upstream fail");
        return NGX_ERROR;
    }
    u = r->upstream;
    up_demo_loc_ctx = ngx_http_get_module_loc_conf(r, ngx_http_upstream_demo_module);
    u->conf = &up_demo_loc_ctx->up_conf;
    //决定转发包体时使用的缓冲区;
    u->buffering = u->conf->buffering;
    u->resolved = ngx_palloc(r->pool, sizeof(ngx_http_upstream_resolved_t));
    if(u->resolved == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "alloc upstream demo upstream resolved mem fail");
        return NGX_ERROR;
    }

    hostent = gethostbyname((char*)"worldtimeapi.org");
    if(NULL == hostent) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream demo get host by name fail");
        return NGX_ERROR;
    }

    sock_addr = ngx_palloc(r->pool, sizeof(struct sockaddr_in));
    sock_addr->sin_family = AF_INET;
    sock_addr->sin_port = htons((in_port_t) 80);
    host = inet_ntoa(*(struct in_addr*) (hostent->h_addr_list[0]));
    sock_addr->sin_addr.s_addr = inet_addr(host);
    up_demo_ctx->addr.data = (u_char *)host;
    up_demo_ctx->addr.len = strlen(host);

    u->resolved->sockaddr = (struct sockaddr *)sock_addr;
    u->resolved->socklen = sizeof(struct sockaddr_in);
    u->resolved->naddrs = 1;
    u->resolved->port = htons((in_port_t) 80);

    u->create_request = ngx_http_upstream_demo_create_request_handler;
    u->process_header = ngx_http_upstream_demo_process_status_line_handler;
    u->finalize_request = ngx_http_upstream_demo_finalize_request;

    r->main->count++;

    ngx_http_upstream_init(r);

    return NGX_DONE;
}

static char* ngx_http_upstream_demo_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t* clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_upstream_demo_handler;
    return NGX_CONF_OK;
}

static ngx_command_t ngx_http_upstream_demo_commands[] = {
    {
        ngx_string("mytest"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LMT_CONF | NGX_CONF_NOARGS,
        ngx_http_upstream_demo_set,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};

void* ngx_http_upstream_demo_loc_ctx_create(ngx_conf_t *cf)
{
    ngx_http_upstream_demo_loc_ctx_t* mlctx = NULL;
    mlctx = ngx_pnalloc(cf->pool, sizeof(ngx_http_upstream_demo_loc_ctx_t));
    
    mlctx->up_conf.connect_timeout = 60000; // ms
    mlctx->up_conf.send_timeout = 60000;
    mlctx->up_conf.read_timeout = 60000; // 不设置，后保 upstream read timeout
    mlctx->up_conf.store_access = 0600;

    //实际上buffering已经决定了将以固定大小的内存作为缓冲区来转发上游的
    //响应包体，这块固定缓冲区的大小就是buffer_size。如果buffering为1
    //就会使用更多的内存缓存来不及发往下游的响应，例如最多使用bufs.num个
    //缓冲区、每个缓冲区大小为bufs.size，另外还会使用临时文件，临时文件的
    //最大长度为max_temp_file_size
    mlctx->up_conf.buffering = 0;
    mlctx->up_conf.buffer_size = ngx_pagesize;
    mlctx->up_conf.bufs.num = 8;
    mlctx->up_conf.bufs.size = ngx_pagesize;
    // 当buffering = 1，busy_buffers_size 赋值给 ngx_event_pipe_t 中的busy
    // busy_size 决定了能缓存的数据的大小。当缓存大于busy_size 时，yao
    // ngx_event_pipe_t 中的busy 时用于保存上次output_filter 没有发送成功的数据。

    mlctx->up_conf.busy_buffers_size = 2 * ngx_pagesize;
    // 写入临时文件，每次写入大小
    mlctx->up_conf.temp_file_write_size = 2 * ngx_pagesize;
    // 临时文件的最大长度
    mlctx->up_conf.max_temp_file_size = 1024 * 1024 * 1024;

    //upstream模块要求hide_headers成员必须要初始化（upstream在解析
    //完上游服务器返回的包头时，会调用
    //ngx_http_upstream_process_headers方法按照hide_headers成员将
    //本应转发给下游的一些http头部隐藏），这里将它赋为
    //NGX_CONF_UNSET_PTR ，是为了在merge合并配置项方法中使用
    //upstream模块提供的ngx_http_upstream_hide_headers_hash
    //方法初始化hide_headers 成员
    mlctx->up_conf.hide_headers = NGX_CONF_UNSET_PTR;
    mlctx->up_conf.pass_headers = NGX_CONF_UNSET_PTR;

    return mlctx;
}

char *ngx_http_upstream_demo_loc_ctx_merge(ngx_conf_t *cf, void *prev, void *conf)
{
    ngx_hash_init_t hinit;
    ngx_http_upstream_demo_loc_ctx_t* ud_prev = prev;
    ngx_http_upstream_demo_loc_ctx_t* ud_conf = conf;


    hinit.max_size = 512;
    hinit.bucket_size = ngx_align(1024, ngx_cacheline_size);
    hinit.name = "upstream_demo_hide_headers_hash";

    if(ngx_http_upstream_hide_headers_hash(cf, &ud_conf->up_conf, &ud_prev->up_conf, ngx_http_upstream_demo_hide_headers, &hinit) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static ngx_http_module_t ngx_http_upstream_demo_module_ctx = {
    NULL,                       /* preconfiguration */
    NULL,                  		/* postconfiguration */

    NULL,                       /* create main configuration */
    NULL,                       /* init main configuration */

    NULL,                       /* create server configuration */
    NULL,                      /* merge server configuration */

    ngx_http_upstream_demo_loc_ctx_create, /* create location configuration */
    ngx_http_upstream_demo_loc_ctx_merge,  /* merge location configuration */
};


ngx_module_t ngx_http_upstream_demo_module = {
    NGX_MODULE_V1,
    &ngx_http_upstream_demo_module_ctx,           /* module context */
    ngx_http_upstream_demo_commands,              /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};