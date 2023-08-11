#include "ngx_core.h"
#include "ngx_config.h"
#include "ngx_http.h"

typedef struct ngx_http_append_prefix_filter_ctx_s {
   ngx_flag_t prefix_appended;
}ngx_http_append_prefix_filter_ctx_t;

typedef struct ngx_http_append_prefix_filter_loc_ctx_s {
     ngx_flag_t enabled;
}ngx_http_append_prefix_filter_loc_ctx_t;

ngx_module_t ngx_http_append_prefix_filter_module;

static ngx_http_output_header_filter_pt ngx_http_header_next_filter;
static ngx_http_output_body_filter_pt ngx_http_body_next_filter;

//将在包体中添加这个前缀
static ngx_str_t prefix_info = ngx_string("[my filter prefix]");

static ngx_command_t ngx_http_append_prefix_filter_commands[] = {
    {
        ngx_string("append_prefix"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LMT_CONF | NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_append_prefix_filter_loc_ctx_t, enabled),
        NULL
    },
    ngx_null_command
};

void* ngx_http_append_prefix_filter_loc_ctx_create(ngx_conf_t *cf)
{
    ngx_http_append_prefix_filter_loc_ctx_t* mlctx = NULL;
    mlctx = ngx_pnalloc(cf->pool, sizeof(ngx_http_append_prefix_filter_loc_ctx_t));

    mlctx->enabled = NGX_CONF_UNSET;

    return mlctx;
}

static char *ngx_http_append_prefix_filter_loc_ctx_merge(ngx_conf_t *cf, void *prev, void *conf)
{
    ngx_http_append_prefix_filter_loc_ctx_t* lc_prev = prev;
    ngx_http_append_prefix_filter_loc_ctx_t* lc_conf = conf;

    ngx_conf_merge_value(lc_conf->enabled, lc_prev->enabled, 0);

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_append_prefix_filter_header_filter(ngx_http_request_t *r)
{
    ngx_http_append_prefix_filter_loc_ctx_t* lc = NULL;
    ngx_http_append_prefix_filter_ctx_t* ctx = NULL;

    if(r->headers_out.status != NGX_HTTP_OK || r != r->main) {/*子请求 不处理*/
        return ngx_http_header_next_filter(r);
    }

    lc = ngx_http_get_module_loc_conf(r, ngx_http_append_prefix_filter_module);
    if(lc->enabled == 0) {/* loc 没有配置 append_prefix on*/
        return ngx_http_header_next_filter(r);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_append_prefix_filter_module);
    if(ctx == NULL) {
        ctx = ngx_palloc(r->pool, sizeof(ngx_http_append_prefix_filter_ctx_t));
        if(ctx == NULL) {
            return NGX_ERROR;
        }
        ngx_http_set_ctx(r, ctx, ngx_http_append_prefix_filter_module);
    }
    else{
        /*ctx 已存在，说明已经处理过了，直接调用next filter*/
        return ngx_http_header_next_filter(r);
    }

    ctx->prefix_appended = 1;
    if(r->headers_out.content_length_n > 0) {
        r->headers_out.content_length_n += prefix_info.len;
    }

    return ngx_http_header_next_filter(r);
}

static ngx_int_t ngx_http_append_prefix_filter_body_filter(ngx_http_request_t *r, ngx_chain_t *out)
{
    ngx_http_append_prefix_filter_ctx_t* ctx;
    ngx_chain_t* chain;
    ngx_buf_t* buf;

    if(r != r->main) {
        return ngx_http_body_next_filter(r, out);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_append_prefix_filter_module);
    if(ctx == NULL || ctx->prefix_appended != 1) {
        return ngx_http_body_next_filter(r, out);
    }

    buf = ngx_calloc_buf(r->pool);
    buf->temporary = 1;
    buf->start = buf->pos = prefix_info.data;
    buf->end = buf->last = buf->start + prefix_info.len;

    chain = ngx_alloc_chain_link(r->pool);
    chain->buf = buf;
    chain->next = out;

    return ngx_http_body_next_filter(r, chain);
}

static ngx_int_t ngx_http_append_prefix_filter_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_header_next_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_append_prefix_filter_header_filter;

    ngx_http_body_next_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_append_prefix_filter_body_filter;

    return NGX_OK;
}

static ngx_http_module_t ngx_http_append_prefix_filter_module_ctx = {
    NULL,                       /* preconfiguration */
    ngx_http_append_prefix_filter_postconfiguration, /* postconfiguration */

    NULL,                       /* create main configuration */
    NULL,                       /* init main configuration */

    NULL,                       /* create server configuration */
    NULL,                      /* merge server configuration */

    ngx_http_append_prefix_filter_loc_ctx_create, /* create location configuration */
    ngx_http_append_prefix_filter_loc_ctx_merge,  /* merge location configuration */
};


ngx_module_t ngx_http_append_prefix_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_append_prefix_filter_module_ctx,           /* module context */
    ngx_http_append_prefix_filter_commands,              /* module directives */
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