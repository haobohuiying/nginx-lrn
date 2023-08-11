#include "ngx_core.h"
#include "ngx_config.h"
#include "ngx_http.h"

typedef struct ngx_http_subrequest_demo_ctx_s {
    ngx_buf_t data;
}ngx_http_subrequest_demo_ctx_t;

ngx_module_t ngx_http_subrequest_demo_module;

static void ngx_http_subrequest_demo_parent_write_event_handler(ngx_http_request_t *r)
{
    ngx_int_t rc;
    ngx_chain_t out_chain;
    ngx_http_subrequest_demo_ctx_t* ctx;

    if(r->headers_out.status != NGX_HTTP_OK) {
        ngx_http_finalize_request(r, r->headers_out.status);
        return;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_subrequest_demo_module);
    if(ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "subrequest demo get ctx null\n");
    }

    ngx_str_set(&r->headers_out.content_type, "application/json; charset=GBK");
    out_chain.buf = &ctx->data;
    out_chain.buf->last_buf = 1; // 不设置不发送last chunck
    out_chain.next = NULL;
    /*  ngx_http_write_filter 中如果没有发送完，就buffered 设置为NGX_HTTP_WRITE_BUFFERED
        ngx_http_finalize_request 检查如果 r->connection->buffered 不为零，会设置连接的write event 为 http_write_handler
    if (r->buffered || c->buffered || r->postponed) {

        if (ngx_http_set_write_handler(r) != NGX_OK) {
            ngx_http_terminate_request(r, 0);
        }

        return;
    }
    所以，不需要在这设置。
    */
    //r->connection->buffered |= NGX_HTTP_WRITE_BUFFERED;
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || r->header_only) {
        ngx_http_finalize_request(r, rc);
        return;
    }

    rc = ngx_http_output_filter(r, &out_chain);
    /*
        注意，这里发送完响应后必须手动调用ngx_http_finalize_request
        结束请求，因为这时http框架不会再帮忙调用它
        ngx_http_request_handler 中调用r->write_event_handler 之后不在调用 ngx_http_finalize_request
    */
    ngx_http_finalize_request(r, rc);

    return;
}

static ngx_int_t ngx_http_subrequest_demo_post_subrequest_handler(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
    ngx_http_request_t* pr;
    ngx_http_subrequest_demo_ctx_t* ctx;

    pr = r->parent;
    //这一步很重要，设置接下来父请求的回调方法
    pr->write_event_handler = ngx_http_subrequest_demo_parent_write_event_handler;
    pr->headers_out.status = r->headers_out.status;
    /* r is subrequest */
    /* ngx_http_upstream_process_headers
    r->headers_out.status = u->headers_in.status_n;*/
    if(rc != NGX_OK || r->headers_out.status != NGX_HTTP_OK) {
        return rc;
    }

    ctx = ngx_http_get_module_ctx(pr, ngx_http_subrequest_demo_module);
    if(ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, pr->connection->log, 0, "subrequest demo get ctx null\n");
        return NGX_ERROR;
    }

    ctx->data = r->upstream->buffer;
    r->upstream->buffer.pos = r->upstream->buffer.last;

    return NGX_OK;
}

static ngx_int_t ngx_http_subrequest_demo_handler(ngx_http_request_t *r)
{
    int rc;
    ngx_str_t sb_uri, sb_uri_pre;
    ngx_http_request_t* sb = NULL;
    ngx_http_post_subrequest_t* psr = NULL;
    ngx_http_subrequest_demo_ctx_t* ctx = NULL;

    ctx = ngx_http_get_module_ctx(r, ngx_http_subrequest_demo_module);
    if(NULL == ctx) {
        ctx = ngx_palloc(r->pool, sizeof(ngx_http_subrequest_demo_ctx_t));
        if(NULL == ctx) {
            return NGX_ERROR;
        }
        ngx_http_set_ctx(r, ctx, ngx_http_subrequest_demo_module);
    }

    ngx_str_set(&sb_uri_pre, "/api/ip");
    sb_uri.len = sb_uri_pre.len + r->args.len;
    sb_uri.data = ngx_palloc(r->pool, sb_uri.len);
    if(sb_uri.data == NULL) {
        return NGX_ERROR;
    }
    ngx_sprintf(sb_uri.data, "%V%V", &sb_uri_pre, &r->args);


    psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
    if(psr == NULL) {
        return NGX_ERROR;
    }
    psr->handler = ngx_http_subrequest_demo_post_subrequest_handler;
    psr->data = ctx;

   // NGX_HTTP_SUBREQUEST_IN_MEMORY参数将告诉upstream模块把上
//游服务器的响应全部保存在子请求的sr->upstream->buffer内存缓冲区中
    rc = ngx_http_subrequest(r, &sb_uri, NULL, &sb, psr, NGX_HTTP_SUBREQUEST_IN_MEMORY);
    if (rc != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_DONE;
}

static char* ngx_http_subrequest_demo_cmd_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t* clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_subrequest_demo_handler;
    return NGX_CONF_OK;
}

static ngx_command_t ngx_http_subrequest_demo_commands[] = {
    {
        ngx_string("sub_request_test"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LMT_CONF | NGX_CONF_NOARGS,
        ngx_http_subrequest_demo_cmd_set,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_subrequest_demo_module_ctx = {
    NULL,                       /* preconfiguration */
    NULL,                  		/* postconfiguration */

    NULL,                       /* create main configuration */
    NULL,                       /* init main configuration */

    NULL,                       /* create server configuration */
    NULL,                      /* merge server configuration */

    NULL, /* create location configuration */
    NULL,  /* merge location configuration */
};


ngx_module_t ngx_http_subrequest_demo_module = {
    NGX_MODULE_V1,
    &ngx_http_subrequest_demo_module_ctx,           /* module context */
    ngx_http_subrequest_demo_commands,              /* module directives */
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