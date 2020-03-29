#include <vector>
#include <cstring>
#include <cstdio>
#include <deque>
#include <unordered_map>
#include <csignal>

#include <libwebsockets.h>
#include <ArduinoJson.h>

#include "secrets.h"

#include "websocket.h"

#include "ha/callbacks.hpp"

bool ha_ready = false;

static struct my_conn {
    lws_sorted_usec_list_t sul;         /* schedule connection retry */
    struct lws *wsi;         /* related wsi if any */
    uint16_t retry_count; /* count of consequetive retries */
} mco;

std::unordered_map<uint16_t, void (*)(const JsonDocument &json)> CallbackMap;

std::deque<std::string> ws_queue;

uint16_t message_id = 50;
uint16_t persist_message_id = 1;

void ws_queue_add(JsonDocument &doc, void (*f)(const JsonDocument &json), bool persist) {
    if (strcmp(doc["type"], "auth") != 0) {
        if (persist) {
            doc["id"] = persist_message_id;
            persist_message_id++;
        } else {
            doc["id"] = message_id;
            message_id++;
        }
        CallbackMap.emplace(doc["id"], f);
    } else {
        CallbackMap.emplace(0, f);
    }
    std::string message;
    serializeJson(doc, message);
    doc.clear();
    ws_queue.push_back(message);
}

void ws_queue_add(JsonDocument &doc) {
    doc["id"] = message_id;
    message_id++;
    std::string message;
    char *out[500];
    serializeJsonPretty(doc, out, 1000);
    printf("%s\n", out);
    serializeJson(doc, message);
    ws_queue.push_back(message);
}

void handle_messages(char *json) {
    char *out[1000];
    StaticJsonDocument<20000> doc;
    StaticJsonDocument<1000> filter;
    filter["type"] = true;
    filter["id"] = true;
    filter["success"] = true;
    filter["result"][0]["area_id"] = true;
    filter["result"][0]["id"] = true;
    filter["result"][0]["name"] = true;
    filter["result"][0]["device_id"] = true;
    filter["result"][0]["entity_id"] = true;
    filter["event"]["event_type"] = true;
    filter["event"]["data"]["entity_id"] = true;
    filter["event"]["data"]["new_state"]["state"] = true;
    filter["event"]["data"]["new_state"]["attributes"]["brightness"] = true;
//    serializeJsonPretty(filter, out, 1000);
//    printf("%s\n", out);



//    DeserializationError error = deserializeJson(doc, json);
    DeserializationError error = deserializeJson(doc, json, DeserializationOption::Filter(filter));

    // Test if parsing succeeds.
    if (error) {
        printf("%s\n", "deserializeJson() failed");
        return;
    }

    serializeJsonPretty(doc, out, 1000);
    printf("%s\n", out);

    const char *type = doc["type"];

    if (strcmp(type, "auth_ok") == 0) {
        CallbackMap[0](doc);
    } else if (strcmp(type, "auth_required") == 0) {
        StaticJsonDocument<300> doc_out;
        doc_out["type"] = "auth";
        doc_out["access_token"] = HA_TOKEN;
        ws_queue_add(doc_out, callback_auth);
    } else {
        uint16_t id = doc["id"];
        if (CallbackMap.count(id) > 0) {
            CallbackMap[id](doc);
            if (id > 50) {
                CallbackMap.erase(id);
            }
        }
    }
}

static struct lws_context *context;
static int interrupted;

/*
 * The retry and backoff policy we want to use for our client connections
 */

static const uint32_t backoff_ms[] = {1000, 2000, 3000, 4000, 5000};

static const lws_retry_bo_t retry = {
        .retry_ms_table            = backoff_ms,
        .retry_ms_table_count        = LWS_ARRAY_SIZE(backoff_ms),
        .conceal_count            = LWS_ARRAY_SIZE(backoff_ms),

        .secs_since_valid_ping        = 3,  /* force PINGs after secs idle */
        .secs_since_valid_hangup    = 10, /* hangup after secs idle */

        .jitter_percent            = 20,
};

/*
 * Scheduled sul callback that starts the connection attempt
 */

static void connect_client(lws_sorted_usec_list_t *sul) {
    struct my_conn *mco = lws_container_of(sul, struct my_conn, sul);
    struct lws_client_connect_info i;

    memset(&i, 0, sizeof(i));

    i.context = context;
    i.port = 8123;
    i.address = HA_ADDRESS;
    i.path = "/api/websocket";
    i.host = i.address;
    i.origin = i.address;
    i.local_protocol_name = "lws-minimal-client";
    i.pwsi = &mco->wsi;
    i.retry_and_idle_policy = &retry;
    i.userdata = mco;

    if (!lws_client_connect_via_info(&i))
        /*
         * Failed... schedule a retry... we can't use the _retry_wsi()
         * convenience wrapper api here because no valid wsi at this
         * point.
         */
        if (lws_retry_sul_schedule(context, 0, sul, &retry,
                                   connect_client, &mco->retry_count)) {
            lwsl_err("%s: connection attempts exhausted\n", __func__);
            interrupted = 1;
        }
}

unsigned char message[LWS_SEND_BUFFER_PRE_PADDING + 500 + LWS_SEND_BUFFER_POST_PADDING];
unsigned char *p = &message[LWS_SEND_BUFFER_PRE_PADDING];

static int callback_minimal(struct lws *wsi, enum lws_callback_reasons reason,
                            void *user, void *in, size_t len) {
    auto *mco = (struct my_conn *) user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_err("CLIENT_CONNECTION_ERROR: %s\n",
                     in ? (char *) in : "(null)");
            goto do_retry;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
//            printf("%s\n", (char *) in);
            handle_messages((char *) in);
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            if (!ws_queue.empty()) {
                auto myIterator = ws_queue.begin();
                sprintf((char *) p, "%s", std::string(*myIterator).c_str());
                printf("string size%zu\n", strlen((char *) p));
                lws_write(wsi, p, strlen((char *) p), LWS_WRITE_TEXT);
                message[LWS_SEND_BUFFER_PRE_PADDING] = '\0';
                ws_queue.pop_front();
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            lwsl_user("%s: established\n", __func__);
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            goto do_retry;

        default:
            break;
    }

    return 0;

    do_retry:
    /*
     * retry the connection to keep it nailed up
     *
     * For this example, we try to conceal any problem for one set of
     * backoff retries and then exit the app.
     *
     * If you set retry.conceal_count to be larger than the number of
     * elements in the backoff table, it will never give up and keep
     * retrying at the last backoff delay plus the random jitter amount.
     */
    interrupted = 1;
//    if (lws_retry_sul_schedule_retry_wsi(wsi, &mco->sul, connect_client,
//                                         &mco->retry_count)) {
//        lwsl_err("%s: connection attempts exhausted\n", __func__);
//        interrupted = 1;
//    }

    return 0;
}

static const struct lws_protocols protocols[] = {
        {"lws-minimal-client", callback_minimal, 0, 100000,},
        {NULL, NULL,                             0, 0}
};

static void sigint_handler(int sig) {
    interrupted = 1;
}

int websocket(void) {
    struct lws_context_creation_info info;

    signal(SIGINT, sigint_handler);
    memset(&info, 0, sizeof info);

    lwsl_user("LWS minimal ws client\n");

    info.port = CONTEXT_PORT_NO_LISTEN; /* we do not run any server */
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("lws init failed\n");
        return 1;
    }

    /* schedule the first client connection attempt to happen immediately */
    lws_sul_schedule(context, 0, &mco.sul, connect_client, 1);

}

int n = 0;

void websocket_thread_func(void) {
    if (n >= 0 && !interrupted) {
        n = lws_service(context, 0);
    }
}