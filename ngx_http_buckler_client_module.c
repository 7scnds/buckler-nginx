#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

ngx_module_t ngx_http_buckler_client_module;

typedef struct {
    ngx_str_t location;
} ngx_http_buckler_header_conf_t;

typedef struct {
    ngx_uint_t status;
    ngx_uint_t done;
} ngx_http_buckler_request_ctx_t;


static ngx_int_t ngx_http_buckler_client_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_buckler_client_handler(ngx_http_request_t *r);
static void *ngx_http_buckler_create_main_conf(ngx_conf_t *cf);
static ngx_int_t ngx_http_buckler_request_done(ngx_http_request_t *r, void *data, ngx_int_t rc);

static ngx_command_t ngx_http_buckler_client_commands[] = {
    {
        ngx_string("buckler_location"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_buckler_header_conf_t, location),
        NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_buckler_client_module_ctx = {
    NULL,                                 /* preconfiguration */
    ngx_http_buckler_client_init,         /* postconfiguration */
    ngx_http_buckler_create_main_conf,    /* create main configuration */
    NULL,                                 /* init main configuration */
    NULL,                                 /* create server configuration */
    NULL,                                 /* merge server configuration */
    NULL,                                 /* create location configuration */
    NULL                                  /* merge location configuration */
};

ngx_module_t ngx_http_buckler_client_module = {
    NGX_MODULE_V1,
    &ngx_http_buckler_client_module_ctx, /* module context */
    ngx_http_buckler_client_commands,    /* module directives */
    NGX_HTTP_MODULE,                     /* module type */
    NULL,                            /* init master */
    NULL,                            /* init module */
    NULL,                            /* init process */
    NULL,                            /* init thread */
    NULL,                            /* exit thread */
    NULL,                            /* exit process */
    NULL,                            /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_buckler_request_done(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
    ngx_http_buckler_request_ctx_t *ctx = data;
    ctx->done = 1;
    ctx->status = r->headers_out.status;

    return rc;
}

static ngx_int_t
ngx_http_buckler_client_handler(ngx_http_request_t *r)
{
    ngx_http_buckler_header_conf_t *bhcf;
    bhcf = ngx_http_get_module_main_conf(r, ngx_http_buckler_client_module);

    if (bhcf == NULL) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "[buckler] proxy location config not found");
        return NGX_DECLINED;
    }

    if (bhcf->location.len <= 0) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "[buckler] proxy location config not found");
        return NGX_DECLINED;
    }

    ngx_http_request_t *sr;
    ngx_http_post_subrequest_t *post_subrequest;
    ngx_http_buckler_request_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_buckler_client_module);

    if (ctx != NULL) {
        if (!ctx->done) {
            return NGX_AGAIN;
        }

        if (ctx->status != NGX_HTTP_TOO_MANY_REQUESTS) {
            if (ctx->status != NGX_HTTP_OK) {
                ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "[buckler] status: %d. make sure buckler is running and your configs are correct", ctx->status);
            }
            return NGX_DECLINED;
        } else {
            return NGX_HTTP_TOO_MANY_REQUESTS;
        }
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_buckler_request_ctx_t));
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "[buckler] could not create context for subrequest");
        return NGX_DECLINED;
    }

    // prevent us from processing twice
    if (r->main->internal) {
        return NGX_DECLINED;
    }
    r->main->internal = 1;

    post_subrequest = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
    if (post_subrequest == NULL) {
        return NGX_DECLINED;
    }

    post_subrequest->handler = ngx_http_buckler_request_done;
    post_subrequest->data = ctx;

    if (ngx_http_subrequest(r, &bhcf->location, NULL, &sr, post_subrequest, NGX_HTTP_SUBREQUEST_WAITED|NGX_HTTP_SUBREQUEST_IN_MEMORY) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "[buckler] failed subrequest. Nginx will process request normally");
        return NGX_DECLINED;
    }

    // ignore subrequest body
    sr->header_only = 1;

    ngx_http_set_ctx(r, ctx, ngx_http_buckler_client_module);

    return NGX_AGAIN;
}

static ngx_int_t
ngx_http_buckler_client_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_buckler_client_handler;

    return NGX_OK;
}

static void *
ngx_http_buckler_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_buckler_header_conf_t *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_buckler_header_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    return conf;
}


