#include "ngx_core.h"
#include "ngx_config.h"
#include "ngx_http.h"

typedef struct ngx_http_mytest_loc_ctx_s {
    ngx_str_t allow_ips;
}ngx_http_mytest_loc_ctx_t;

ngx_module_t ngx_http_mytest_module;

ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r)
{
    ngx_int_t rc;
    ngx_chain_t chain;
    ngx_str_t type = ngx_string("text/plain");
    ngx_str_t response = ngx_string("Hello World\r\n");
    ngx_buf_t* buf;

    if(!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
        return  NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);
    if(rc != NGX_OK) {
        return rc;
    }

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_type_len = type.len;
    r->headers_out.content_type = type;

    rc = ngx_http_send_header(r); // rc > 0 返回是http status
    if(rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
    
    buf = ngx_create_temp_buf(r->pool, response.len);
    if(NULL == buf) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_memcpy(buf->pos, response.data, response.len);
    buf->last = buf->pos + response.len;
    buf->last_buf = 1;

    chain.buf = buf;
    chain.next = NULL;
    return ngx_http_output_filter(r, &chain);
}

static char* ngx_http_mytest_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t* clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_mytest_handler;
    return NGX_CONF_OK;
}

static ngx_command_t ngx_http_mytest_commands[] = {
    {
        ngx_string("mytest"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LMT_CONF | NGX_CONF_NOARGS,
        ngx_http_mytest_set,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    {
        ngx_string("mytest_allow_ips"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LMT_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_mytest_loc_ctx_t, allow_ips),
        NULL
    },
    ngx_null_command
};

static ngx_int_t ngx_http_mytest_http_checkAllowIps(ngx_http_request_t *r)
{
    ngx_http_mytest_loc_ctx_t* mlctx = NULL;

    mlctx = ngx_http_get_module_loc_conf(r, ngx_http_mytest_module);
    if(ngx_strstr(r->connection->addr_text.data, mlctx->allow_ips.data) != NULL) {
        return NGX_OK;
    }

    return NGX_HTTP_FORBIDDEN;
}

static ngx_int_t  ngx_http_mytest_http_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_handler_pt* h = NULL;
    ngx_http_core_main_conf_t* cmcf = NULL;


    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if(NULL == h) {
        return NGX_ERROR;
    }

    *h = ngx_http_mytest_http_checkAllowIps;
    return NGX_OK;
}

void* ngx_http_mytest_loc_ctx_create(ngx_conf_t *cf)
{
    ngx_http_mytest_loc_ctx_t* mlctx = NULL;
    mlctx = ngx_pnalloc(cf->pool, sizeof(ngx_http_mytest_loc_ctx_t));
    //mlctx->allow_ips = ngx_pnalloc(cf->pool, sizeof(ngx_str_t));
    //ngx_str_null(mlctx.allow_ips);
    return mlctx;
}

static ngx_http_module_t ngx_http_mytest_module_ctx = {
    NULL,                       /* preconfiguration */
    ngx_http_mytest_http_postconfiguration,                  		/* postconfiguration */

    NULL,                       /* create main configuration */
    NULL,                       /* init main configuration */

    NULL,                       /* create server configuration */
    NULL,                      /* merge server configuration */

    ngx_http_mytest_loc_ctx_create, /* create location configuration */
    NULL         			/* merge location configuration */
};


ngx_module_t ngx_http_mytest_module = {
    NGX_MODULE_V1,
    &ngx_http_mytest_module_ctx,           /* module context */
    ngx_http_mytest_commands,              /* module directives */
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