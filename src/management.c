/*
 * Copyright (C) 2023-24 Hamish Coleman
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Common routines shared between the management interfaces
 *
 */


#include <connslot/connslot.h>  // for conn_t
#include <n3n/logging.h> // for traceEvent
#include <pearson.h>     // for pearson_hash_64
#include <stdbool.h>
#include <stdio.h>       // for snprintf, NULL, size_t
#include <stdlib.h>      // for strtoul
#include <string.h>      // for strtok, strlen, strncpy
#include "management.h"

#ifdef _WIN32
#include "win32/defs.h"
#else
#include <netdb.h>       // for getnameinfo, NI_NUMERICHOST, NI_NUMERICSERV
#include <sys/socket.h>  // for sendto, sockaddr
#endif


ssize_t send_reply (mgmt_req_t *req, strbuf_t *buf) {
    // TODO: better error handling (counters?)
    return sendto(req->mgmt_sock, buf->str, buf->wr_pos, 0,
                  &req->sender_sock, req->sock_len);
}

size_t gen_json_1str (strbuf_t *buf, char *tag, char *_type, char *key, char *val) {
    return sb_printf(buf,
                     "{"
                     "\"_tag\":\"%s\","
                     "\"_type\":\"%s\","
                     "\"%s\":\"%s\"}\n",
                     tag,
                     _type,
                     key,
                     val);
}

size_t gen_json_1uint (strbuf_t *buf, char *tag, char *_type, char *key, unsigned int val) {
    return sb_printf(buf,
                     "{"
                     "\"_tag\":\"%s\","
                     "\"_type\":\"%s\","
                     "\"%s\":%u}\n",
                     tag,
                     _type,
                     key,
                     val);
}

void send_json_1str (mgmt_req_t *req, strbuf_t *buf, char *_type, char *key, char *val) {
    gen_json_1str(buf, req->tag, _type, key, val);
    send_reply(req, buf);
}

void send_json_1uint (mgmt_req_t *req, strbuf_t *buf, char *_type, char *key, unsigned int val) {
    gen_json_1uint(buf, req->tag, _type, key, val);
    send_reply(req, buf);
}

void mgmt_error (mgmt_req_t *req, strbuf_t *buf, char *msg) {
    send_json_1str(req, buf, "error", "error", msg);
}

void mgmt_stop (mgmt_req_t *req, strbuf_t *buf) {

    if(req->type==N2N_MGMT_WRITE) {
        *req->keep_running = false;
    }

    send_json_1uint(req, buf, "row", "keep_running", *req->keep_running);
}

void mgmt_verbose (mgmt_req_t *req, strbuf_t *buf) {

    if(req->type==N2N_MGMT_WRITE) {
        if(req->argv) {
            setTraceLevel(strtoul(req->argv, NULL, 0));
        }
    }

    send_json_1uint(req, buf, "row", "traceLevel", getTraceLevel());
}

void mgmt_unimplemented (mgmt_req_t *req, strbuf_t *buf) {

    mgmt_error(req, buf, "unimplemented");
}

void mgmt_event_post2 (enum n2n_event_topic topic, int data0, void *data1, mgmt_req_t *debug, mgmt_req_t *sub, mgmt_event_handler_t fn) {
    traceEvent(TRACE_DEBUG, "post topic=%i data0=%i", topic, data0);

    if( sub->type != N2N_MGMT_SUB && debug->type != N2N_MGMT_SUB) {
        // If neither of this topic or the debug topic have a subscriber
        // then we dont need to do any work
        return;
    }

    char buf_space[100];
    strbuf_t *buf;
    STRBUF_INIT(buf, buf_space);

    char *tag;
    if(sub->type == N2N_MGMT_SUB) {
        tag = sub->tag;
    } else {
        tag = debug->tag;
    }

    fn(buf, tag, data0, data1);

    if(sub->type == N2N_MGMT_SUB) {
        send_reply(sub, buf);
    }
    if(debug->type == N2N_MGMT_SUB) {
        send_reply(debug, buf);
    }
    // TODO:
    // - ideally, we would detect that the far end has gone away and
    //   set the ->type back to N2N_MGMT_UNKNOWN, but we are not using
    //   a connected socket, so that is difficult
    // - failing that, we should require the client to send an unsubscribe
    //   and provide a manual unsubscribe
}

void mgmt_help_row (mgmt_req_t *req, strbuf_t *buf, char *cmd, char *help) {
    sb_printf(buf,
              "{"
              "\"_tag\":\"%s\","
              "\"_type\":\"row\","
              "\"cmd\":\"%s\","
              "\"help\":\"%s\"}\n",
              req->tag,
              cmd,
              help);

    send_reply(req, buf);
}

void mgmt_help_events_row (mgmt_req_t *req, strbuf_t *buf, mgmt_req_t *sub, char *cmd, char *help) {
    char host[40];
    char serv[6];

    if((sub->type != N2N_MGMT_SUB) ||
       getnameinfo((struct sockaddr *)&sub->sender_sock, sizeof(sub->sender_sock),
                   host, sizeof(host),
                   serv, sizeof(serv),
                   NI_NUMERICHOST|NI_NUMERICSERV) != 0) {
        host[0] = '?';
        host[1] = 0;
        serv[0] = '?';
        serv[1] = 0;
    }

    // TODO: handle a topic with no subscribers more cleanly

    sb_printf(buf,
              "{"
              "\"_tag\":\"%s\","
              "\"_type\":\"row\","
              "\"topic\":\"%s\","
              "\"tag\":\"%s\","
              "\"sockaddr\":\"%s:%s\","
              "\"help\":\"%s\"}\n",
              req->tag,
              cmd,
              sub->tag,
              host, serv,
              help);

    send_reply(req, buf);
}

// TODO: work out a method to keep the mgmt_handlers defintion const static,
// and then import the shared mgmt_help () definition to this file

/*
 * Check if the user is authorised for this command.
 * - this should be more configurable!
 * - for the moment we use some simple heuristics:
 *   Reads are not dangerous, so they are simply allowed
 *   Writes are possibly dangerous, so they need a fake password
 */
int mgmt_auth (mgmt_req_t *req, char *auth) {

    if(auth) {
        /* If we have an auth key, it must match */
        if(!strcmp(req->mgmt_password, auth)) {
            return 1;
        }
        return 0;
    }
    /* if we dont have an auth key, we can still read */
    if(req->type == N2N_MGMT_READ) {
        return 1;
    }

    return 0;
}

/*
 * Handle the common and shred parts of the mgmt_req_t initialisation
 */
bool mgmt_req_init2 (mgmt_req_t *req, strbuf_t *buf, char *cmdline) {
    char *typechar;
    char *options;
    char *flagstr;
    int flags;
    char *auth;

    /* Initialise the tag field until we extract it from the cmdline */
    req->tag[0] = '-';
    req->tag[1] = '1';
    req->tag[2] = '\0';

    typechar = strtok(cmdline, " \r\n");
    if(!typechar) {
        /* should not happen */
        mgmt_error(req, buf, "notype");
        return false;
    }
    if(*typechar == 'r') {
        req->type=N2N_MGMT_READ;
    } else if(*typechar == 'w') {
        req->type=N2N_MGMT_WRITE;
    } else if(*typechar == 's') {
        req->type=N2N_MGMT_SUB;
    } else {
        mgmt_error(req, buf, "badtype");
        return false;
    }

    /* Extract the tag to use in all reply packets */
    options = strtok(NULL, " \r\n");
    if(!options) {
        mgmt_error(req, buf, "nooptions");
        return false;
    }

    req->argv0 = strtok(NULL, " \r\n");
    if(!req->argv0) {
        mgmt_error(req, buf, "nocmd");
        return false;
    }

    /*
     * The entire rest of the line is the argv. We apply no processing
     * or arg separation so that the cmd can use it however it needs.
     */
    req->argv = strtok(NULL, "\r\n");

    /*
     * There might be an auth token mixed in with the tag
     */
    char *tagp = strtok(options, ":");
    strncpy(req->tag, tagp, sizeof(req->tag)-1);
    req->tag[sizeof(req->tag)-1] = '\0';

    flagstr = strtok(NULL, ":");
    if(flagstr) {
        flags = strtoul(flagstr, NULL, 16);
    } else {
        flags = 0;
    }

    /* Only 1 flag bit defined at the moment - "auth option present" */
    if(flags & 1) {
        auth = strtok(NULL, ":");
    } else {
        auth = NULL;
    }

    if(!mgmt_auth(req, auth)) {
        mgmt_error(req, buf, "badauth");
        return false;
    }

    return true;
}

void render_http (conn_t *conn, int code) {
    strbuf_t **pp = &conn->reply_header;
    sb_reprintf(pp, "HTTP/1.1 %i result\r\n", code);
    // TODO:
    // - content type
    // - caching
    int len = sb_len(conn->reply);
    sb_reprintf(pp, "Content-Length: %i\r\n\r\n", len);
}

void render_error (n2n_edge_t *eee, conn_t *conn) {
    // Reuse the request buffer
    conn->reply = conn->request;
    sb_zero(conn->reply);
    sb_printf(conn->reply, "api error\n");

    render_http(conn, 404);
}

#include "management_index.html.h"

void render_index_page (n2n_edge_t *eee, conn_t *conn) {
    conn->reply = &management_index;
    render_http(conn, 200);
}

#include "management_script.js.h"

void render_script_page (n2n_edge_t *eee, conn_t *conn) {
    conn->reply = &management_script;
    render_http(conn, 200);
}

struct mgmt_api_endpoint {
    char *match;    // when the request buffer starts with this
    void (*func)(n2n_edge_t *eee, conn_t *conn);
    char *desc;
};

static struct mgmt_api_endpoint api_endpoints[] = {
    { "POST /v1 ", render_error, "JsonRPC" },
    { "GET / ", render_index_page, "Human interface" },
    { "GET /script.js ", render_script_page, "javascript helpers" },
    // status
    // metrics
    // help
};

void mgmt_api_handler (n2n_edge_t *eee, conn_t *conn) {
    int i;
    int nr_handlers = sizeof(api_endpoints) / sizeof(api_endpoints[0]);
    for( i=0; i < nr_handlers; i++ ) {
        if(!strncmp(
               api_endpoints[i].match,
               conn->request->str,
               strlen(api_endpoints[i].match))) {
            break;
        }
    }
    if( i >= nr_handlers ) {
        render_error(eee, conn);
    } else {
        api_endpoints[i].func(eee, conn);
    }

    // Try to immediately start sending the reply
    conn_write(conn);
}
