#include "libdatachannel_wrapper.h"

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtc/rtc.h"

#ifndef RTC_ERR_SUCCESS
#define RTC_ERR_SUCCESS 0
#endif

#ifndef RTC_ERR_INVALID
#define RTC_ERR_INVALID -1
#endif

#ifndef RTC_ERR_FAILURE
#define RTC_ERR_FAILURE -2
#endif

#ifndef RTC_ERR_NOT_AVAIL
#define RTC_ERR_NOT_AVAIL -3
#endif

#ifndef RTC_ERR_TOO_SMALL
#define RTC_ERR_TOO_SMALL -4
#endif

typedef struct ldc_pc_ctx {
    int32_t pc;
    ldc_local_description_cb on_local_description;
    void *on_local_description_user;
    ldc_local_candidate_cb on_local_candidate;
    void *on_local_candidate_user;
    ldc_state_cb on_state;
    void *on_state_user;
    ldc_ice_state_cb on_ice_state;
    void *on_ice_state_user;
    ldc_gathering_state_cb on_gathering_state;
    void *on_gathering_state_user;
    ldc_signaling_state_cb on_signaling_state;
    void *on_signaling_state_user;
    ldc_data_channel_cb on_data_channel;
    void *on_data_channel_user;
    ldc_track_cb on_track;
    void *on_track_user;
    struct ldc_pc_ctx *next;
} ldc_pc_ctx;

typedef struct ldc_id_ctx {
    int32_t id;
    ldc_open_cb on_open;
    void *on_open_user;
    ldc_closed_cb on_closed;
    void *on_closed_user;
    ldc_error_cb on_error;
    void *on_error_user;
    ldc_message_cb on_message;
    void *on_message_user;
    struct ldc_id_ctx *next;
} ldc_id_ctx;

struct ldc_wrapper {
    void *lib;
    pthread_mutex_t mu;
    char last_error[512];

    int (*rtcCreatePeerConnection)(const rtcConfiguration *config);
    int (*rtcClosePeerConnection)(int pc);
    int (*rtcDeletePeerConnection)(int pc);

    int (*rtcSetLocalDescriptionCallback)(int pc, rtcDescriptionCallbackFunc cb);
    int (*rtcSetLocalCandidateCallback)(int pc, rtcCandidateCallbackFunc cb);
    int (*rtcSetStateChangeCallback)(int pc, rtcStateChangeCallbackFunc cb);
    int (*rtcSetIceStateChangeCallback)(int pc, rtcIceStateChangeCallbackFunc cb);
    int (*rtcSetGatheringStateChangeCallback)(int pc, rtcGatheringStateCallbackFunc cb);
    int (*rtcSetSignalingStateChangeCallback)(int pc, rtcSignalingStateCallbackFunc cb);
    int (*rtcSetDataChannelCallback)(int pc, rtcDataChannelCallbackFunc cb);
    int (*rtcSetTrackCallback)(int pc, rtcTrackCallbackFunc cb);

    int (*rtcSetLocalDescription)(int pc, const char *type);
    int (*rtcSetRemoteDescription)(int pc, const char *sdp, const char *type);
    int (*rtcAddRemoteCandidate)(int pc, const char *cand, const char *mid);

    int (*rtcGetLocalDescription)(int pc, char *buffer, int size);
    int (*rtcGetRemoteDescription)(int pc, char *buffer, int size);
    int (*rtcGetLocalDescriptionType)(int pc, char *buffer, int size);
    int (*rtcGetRemoteDescriptionType)(int pc, char *buffer, int size);

    int (*rtcAddTrackEx)(int pc, const rtcTrackInit *init);

    int (*rtcCreateDataChannel)(int pc, const char *label);
    int (*rtcCreateDataChannelEx)(int pc, const char *label, const rtcDataChannelInit *init);

    int (*rtcSetOpenCallback)(int id, rtcOpenCallbackFunc cb);
    int (*rtcSetClosedCallback)(int id, rtcClosedCallbackFunc cb);
    int (*rtcSetErrorCallback)(int id, rtcErrorCallbackFunc cb);
    int (*rtcSetMessageCallback)(int id, rtcMessageCallbackFunc cb);

    int (*rtcSendMessage)(int id, const char *data, int size);
    int (*rtcClose)(int id);
    int (*rtcDelete)(int id);
    bool (*rtcIsOpen)(int id);
    bool (*rtcIsClosed)(int id);

    void (*rtcSetUserPointer)(int id, void *ptr);

    ldc_pc_ctx *pcs;
    ldc_id_ctx *ids;
};

static void ldc_set_last_error(ldc_wrapper_t *h, const char *msg) {
    if (!h) return;
    if (!msg) {
        h->last_error[0] = '\0';
        return;
    }

    snprintf(h->last_error, sizeof(h->last_error), "%s", msg);
}

static void *ldc_load_symbol(ldc_wrapper_t *h, const char *name) {
    dlerror();
    void *sym = dlsym(h->lib, name);
    const char *err = dlerror();
    if (err != NULL) {
        char buf[512];
        snprintf(buf, sizeof(buf), "missing symbol %s: %s", name, err);
        ldc_set_last_error(h, buf);
        return NULL;
    }

    return sym;
}

static ldc_pc_ctx *ldc_find_pc_ctx(ldc_wrapper_t *h, int32_t pc) {
    for (ldc_pc_ctx *it = h->pcs; it != NULL; it = it->next) {
        if (it->pc == pc) return it;
    }

    return NULL;
}

static ldc_pc_ctx *ldc_detach_pc_ctx(ldc_wrapper_t *h, int32_t pc) {
    ldc_pc_ctx **pp = &h->pcs;
    while (*pp != NULL) {
        if ((*pp)->pc == pc) {
            ldc_pc_ctx *ctx = *pp;
            *pp = ctx->next;
            ctx->next = NULL;
            return ctx;
        }

        pp = &(*pp)->next;
    }

    return NULL;
}

static ldc_id_ctx *ldc_find_id_ctx(ldc_wrapper_t *h, int32_t id) {
    for (ldc_id_ctx *it = h->ids; it != NULL; it = it->next) {
        if (it->id == id) return it;
    }

    return NULL;
}

static ldc_id_ctx *ldc_get_or_create_id_ctx(ldc_wrapper_t *h, int32_t id) {
    ldc_id_ctx *ctx = ldc_find_id_ctx(h, id);
    if (ctx) return ctx;

    ctx = (ldc_id_ctx *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->id = id;
    ctx->next = h->ids;
    h->ids = ctx;
    return ctx;
}

static ldc_id_ctx *ldc_detach_id_ctx(ldc_wrapper_t *h, int32_t id) {
    ldc_id_ctx **pp = &h->ids;
    while (*pp != NULL) {
        if ((*pp)->id == id) {
            ldc_id_ctx *ctx = *pp;
            *pp = ctx->next;
            ctx->next = NULL;
            return ctx;
        }

        pp = &(*pp)->next;
    }

    return NULL;
}

static void ldc_disable_pc_callbacks(ldc_wrapper_t *h, int32_t pc) {
    if (h->rtcSetLocalDescriptionCallback) h->rtcSetLocalDescriptionCallback(pc, NULL);
    if (h->rtcSetLocalCandidateCallback) h->rtcSetLocalCandidateCallback(pc, NULL);
    if (h->rtcSetStateChangeCallback) h->rtcSetStateChangeCallback(pc, NULL);
    if (h->rtcSetIceStateChangeCallback) h->rtcSetIceStateChangeCallback(pc, NULL);
    if (h->rtcSetGatheringStateChangeCallback) h->rtcSetGatheringStateChangeCallback(pc, NULL);
    if (h->rtcSetSignalingStateChangeCallback) h->rtcSetSignalingStateChangeCallback(pc, NULL);
    if (h->rtcSetDataChannelCallback) h->rtcSetDataChannelCallback(pc, NULL);
    if (h->rtcSetTrackCallback) h->rtcSetTrackCallback(pc, NULL);
}

static void ldc_disable_id_callbacks(ldc_wrapper_t *h, int32_t id) {
    if (h->rtcSetOpenCallback) h->rtcSetOpenCallback(id, NULL);
    if (h->rtcSetClosedCallback) h->rtcSetClosedCallback(id, NULL);
    if (h->rtcSetErrorCallback) h->rtcSetErrorCallback(id, NULL);
    if (h->rtcSetMessageCallback) h->rtcSetMessageCallback(id, NULL);
}

static void RTC_API ldc_local_description_bridge(int pc, const char *sdp, const char *type, void *ptr) {
    ldc_pc_ctx *ctx = (ldc_pc_ctx *)ptr;
    if (ctx != NULL && ctx->on_local_description != NULL) {
        ctx->on_local_description(pc, sdp, type, ctx->on_local_description_user);
    }
}

static void RTC_API ldc_local_candidate_bridge(int pc, const char *cand, const char *mid, void *ptr) {
    ldc_pc_ctx *ctx = (ldc_pc_ctx *)ptr;
    if (ctx != NULL && ctx->on_local_candidate != NULL) {
        ctx->on_local_candidate(pc, cand, mid, ctx->on_local_candidate_user);
    }
}

static void RTC_API ldc_state_bridge(int pc, rtcState state, void *ptr) {
    ldc_pc_ctx *ctx = (ldc_pc_ctx *)ptr;
    if (ctx != NULL && ctx->on_state != NULL) {
        ctx->on_state(pc, (int32_t)state, ctx->on_state_user);
    }
}

static void RTC_API ldc_ice_state_bridge(int pc, rtcIceState state, void *ptr) {
    ldc_pc_ctx *ctx = (ldc_pc_ctx *)ptr;
    if (ctx != NULL && ctx->on_ice_state != NULL) {
        ctx->on_ice_state(pc, (int32_t)state, ctx->on_ice_state_user);
    }
}

static void RTC_API ldc_gathering_state_bridge(int pc, rtcGatheringState state, void *ptr) {
    ldc_pc_ctx *ctx = (ldc_pc_ctx *)ptr;
    if (ctx != NULL && ctx->on_gathering_state != NULL) {
        ctx->on_gathering_state(pc, (int32_t)state, ctx->on_gathering_state_user);
    }
}

static void RTC_API ldc_signaling_state_bridge(int pc, rtcSignalingState state, void *ptr) {
    ldc_pc_ctx *ctx = (ldc_pc_ctx *)ptr;
    if (ctx != NULL && ctx->on_signaling_state != NULL) {
        ctx->on_signaling_state(pc, (int32_t)state, ctx->on_signaling_state_user);
    }
}

static void RTC_API ldc_data_channel_bridge(int pc, int dc, void *ptr) {
    ldc_pc_ctx *ctx = (ldc_pc_ctx *)ptr;
    if (ctx != NULL && ctx->on_data_channel != NULL) {
        ctx->on_data_channel(pc, dc, ctx->on_data_channel_user);
    }
}

static void RTC_API ldc_track_bridge(int pc, int tr, void *ptr) {
    ldc_pc_ctx *ctx = (ldc_pc_ctx *)ptr;
    if (ctx != NULL && ctx->on_track != NULL) {
        ctx->on_track(pc, tr, ctx->on_track_user);
    }
}

static void RTC_API ldc_open_bridge(int id, void *ptr) {
    ldc_id_ctx *ctx = (ldc_id_ctx *)ptr;
    if (ctx != NULL && ctx->on_open != NULL) {
        ctx->on_open(id, ctx->on_open_user);
    }
}

static void RTC_API ldc_closed_bridge(int id, void *ptr) {
    ldc_id_ctx *ctx = (ldc_id_ctx *)ptr;
    if (ctx != NULL && ctx->on_closed != NULL) {
        ctx->on_closed(id, ctx->on_closed_user);
    }
}

static void RTC_API ldc_error_bridge(int id, const char *error, void *ptr) {
    ldc_id_ctx *ctx = (ldc_id_ctx *)ptr;
    if (ctx != NULL && ctx->on_error != NULL) {
        ctx->on_error(id, error, ctx->on_error_user);
    }
}

static void RTC_API ldc_message_bridge(int id, const char *message, int size, void *ptr) {
    ldc_id_ctx *ctx = (ldc_id_ctx *)ptr;
    if (ctx != NULL && ctx->on_message != NULL && size >= 0) {
        ctx->on_message(id, (const uint8_t *)message, (size_t)size, ctx->on_message_user);
    }
}

static int32_t ldc_read_rtc_string(
    int32_t (*getter)(int, char *, int),
    int32_t id,
    uint8_t **out_ptr,
    size_t *out_len
) {
    if (!getter || !out_ptr || !out_len) return RTC_ERR_INVALID;

    int cap = 4096;
    char *buf = NULL;

    while (cap <= (1024 * 1024)) {
        char *next = (char *)realloc(buf, (size_t)cap);
        if (!next) {
            free(buf);
            return RTC_ERR_FAILURE;
        }
        buf = next;

        const int rc = getter(id, buf, cap);
        if (rc == RTC_ERR_TOO_SMALL || rc >= cap) {
            cap *= 2;
            continue;
        }
        if (rc < 0) {
            free(buf);
            return rc;
        }

        *out_ptr = (uint8_t *)buf;
        *out_len = (size_t)rc;
        return RTC_ERR_SUCCESS;
    }

    free(buf);
    return RTC_ERR_TOO_SMALL;
}

int32_t ldc_wrapper_create(ldc_wrapper_t **out_handle, const char *lib_path) {
    if (!out_handle) return RTC_ERR_INVALID;
    *out_handle = NULL;

    ldc_wrapper_t *h = (ldc_wrapper_t *)calloc(1, sizeof(*h));
    if (!h) return RTC_ERR_FAILURE;

    pthread_mutex_init(&h->mu, NULL);

    const char *path = (lib_path && lib_path[0] != '\0') ? lib_path : "libdatachannel.so";
    h->lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h->lib) {
        ldc_set_last_error(h, dlerror());
        pthread_mutex_destroy(&h->mu);
        free(h);
        return RTC_ERR_FAILURE;
    }

#define LDC_LOAD_REQUIRED(name)                                                                         \
    do {                                                                                                \
        h->name = ldc_load_symbol(h, #name);                                                           \
        if (!h->name) {                                                                                 \
            dlclose(h->lib);                                                                            \
            pthread_mutex_destroy(&h->mu);                                                              \
            free(h);                                                                                    \
            return RTC_ERR_FAILURE;                                                                     \
        }                                                                                               \
    } while (0)

#define LDC_LOAD_OPTIONAL(name)                                                                         \
    do {                                                                                                \
        dlerror();                                                                                      \
        h->name = dlsym(h->lib, #name);                                                                 \
        (void)dlerror();                                                                                \
    } while (0)

    LDC_LOAD_REQUIRED(rtcCreatePeerConnection);
    LDC_LOAD_OPTIONAL(rtcClosePeerConnection);
    LDC_LOAD_REQUIRED(rtcDeletePeerConnection);

    LDC_LOAD_REQUIRED(rtcSetLocalDescriptionCallback);
    LDC_LOAD_REQUIRED(rtcSetLocalCandidateCallback);
    LDC_LOAD_REQUIRED(rtcSetStateChangeCallback);
    LDC_LOAD_REQUIRED(rtcSetIceStateChangeCallback);
    LDC_LOAD_REQUIRED(rtcSetGatheringStateChangeCallback);
    LDC_LOAD_REQUIRED(rtcSetSignalingStateChangeCallback);
    LDC_LOAD_REQUIRED(rtcSetDataChannelCallback);
    LDC_LOAD_REQUIRED(rtcSetTrackCallback);

    LDC_LOAD_REQUIRED(rtcSetLocalDescription);
    LDC_LOAD_REQUIRED(rtcSetRemoteDescription);
    LDC_LOAD_REQUIRED(rtcAddRemoteCandidate);

    LDC_LOAD_REQUIRED(rtcGetLocalDescription);
    LDC_LOAD_REQUIRED(rtcGetRemoteDescription);
    LDC_LOAD_REQUIRED(rtcGetLocalDescriptionType);
    LDC_LOAD_REQUIRED(rtcGetRemoteDescriptionType);

    LDC_LOAD_REQUIRED(rtcAddTrackEx);

    LDC_LOAD_REQUIRED(rtcCreateDataChannel);
    LDC_LOAD_OPTIONAL(rtcCreateDataChannelEx);

    LDC_LOAD_REQUIRED(rtcSetOpenCallback);
    LDC_LOAD_REQUIRED(rtcSetClosedCallback);
    LDC_LOAD_REQUIRED(rtcSetErrorCallback);
    LDC_LOAD_REQUIRED(rtcSetMessageCallback);

    LDC_LOAD_REQUIRED(rtcSendMessage);
    LDC_LOAD_REQUIRED(rtcClose);
    LDC_LOAD_REQUIRED(rtcDelete);
    LDC_LOAD_REQUIRED(rtcIsOpen);
    LDC_LOAD_REQUIRED(rtcIsClosed);

    LDC_LOAD_REQUIRED(rtcSetUserPointer);

#undef LDC_LOAD_REQUIRED
#undef LDC_LOAD_OPTIONAL

    ldc_set_last_error(h, "");
    *out_handle = h;
    return RTC_ERR_SUCCESS;
}

void ldc_wrapper_destroy(ldc_wrapper_t *handle) {
    if (!handle) return;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *pc_head = handle->pcs;
    ldc_id_ctx *id_head = handle->ids;
    handle->pcs = NULL;
    handle->ids = NULL;
    pthread_mutex_unlock(&handle->mu);

    for (ldc_pc_ctx *it = pc_head; it != NULL;) {
        ldc_pc_ctx *next = it->next;
        ldc_disable_pc_callbacks(handle, it->pc);
        handle->rtcSetUserPointer(it->pc, NULL);
        handle->rtcDeletePeerConnection(it->pc);
        free(it);
        it = next;
    }

    for (ldc_id_ctx *it = id_head; it != NULL;) {
        ldc_id_ctx *next = it->next;
        ldc_disable_id_callbacks(handle, it->id);
        handle->rtcSetUserPointer(it->id, NULL);
        free(it);
        it = next;
    }

    if (handle->lib) dlclose(handle->lib);
    pthread_mutex_destroy(&handle->mu);
    free(handle);
}

int32_t ldc_create_peer_connection(ldc_wrapper_t *handle, const char *stun_url, int32_t *out_pc) {
    if (!handle || !out_pc) return RTC_ERR_INVALID;

    const char *servers[1];
    rtcConfiguration cfg;
    memset(&cfg, 0, sizeof(cfg));

    if (stun_url && stun_url[0] != '\0') {
        servers[0] = stun_url;
        cfg.iceServers = servers;
        cfg.iceServersCount = 1;
    }

    const int pc = handle->rtcCreatePeerConnection(&cfg);
    if (pc < 0) {
        ldc_set_last_error(handle, "rtcCreatePeerConnection failed");
        return pc;
    }

    ldc_pc_ctx *ctx = (ldc_pc_ctx *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        handle->rtcDeletePeerConnection(pc);
        return RTC_ERR_FAILURE;
    }

    ctx->pc = pc;

    pthread_mutex_lock(&handle->mu);
    ctx->next = handle->pcs;
    handle->pcs = ctx;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    *out_pc = pc;
    return RTC_ERR_SUCCESS;
}

int32_t ldc_close_peer_connection(ldc_wrapper_t *handle, int32_t pc) {
    if (!handle) return RTC_ERR_INVALID;
    if (!handle->rtcClosePeerConnection) return RTC_ERR_NOT_AVAIL;
    return handle->rtcClosePeerConnection(pc);
}

int32_t ldc_delete_peer_connection(ldc_wrapper_t *handle, int32_t pc) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_detach_pc_ctx(handle, pc);
    pthread_mutex_unlock(&handle->mu);

    if (ctx != NULL) {
        ldc_disable_pc_callbacks(handle, pc);
        handle->rtcSetUserPointer(pc, NULL);
    }

    const int rc = handle->rtcDeletePeerConnection(pc);

    if (ctx != NULL) {
        free(ctx);
    }

    return rc;
}

int32_t ldc_set_local_description_callback(ldc_wrapper_t *handle, int32_t pc, ldc_local_description_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_find_pc_ctx(handle, pc);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_INVALID;
    }
    ctx->on_local_description = cb;
    ctx->on_local_description_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    return handle->rtcSetLocalDescriptionCallback(pc, cb ? ldc_local_description_bridge : NULL);
}

int32_t ldc_set_local_candidate_callback(ldc_wrapper_t *handle, int32_t pc, ldc_local_candidate_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_find_pc_ctx(handle, pc);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_INVALID;
    }
    ctx->on_local_candidate = cb;
    ctx->on_local_candidate_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    return handle->rtcSetLocalCandidateCallback(pc, cb ? ldc_local_candidate_bridge : NULL);
}

int32_t ldc_set_state_callback(ldc_wrapper_t *handle, int32_t pc, ldc_state_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_find_pc_ctx(handle, pc);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_INVALID;
    }
    ctx->on_state = cb;
    ctx->on_state_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    return handle->rtcSetStateChangeCallback(pc, cb ? ldc_state_bridge : NULL);
}

int32_t ldc_set_ice_state_callback(ldc_wrapper_t *handle, int32_t pc, ldc_ice_state_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_find_pc_ctx(handle, pc);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_INVALID;
    }
    ctx->on_ice_state = cb;
    ctx->on_ice_state_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    return handle->rtcSetIceStateChangeCallback(pc, cb ? ldc_ice_state_bridge : NULL);
}

int32_t ldc_set_gathering_state_callback(
    ldc_wrapper_t *handle,
    int32_t pc,
    ldc_gathering_state_cb cb,
    void *user
) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_find_pc_ctx(handle, pc);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_INVALID;
    }
    ctx->on_gathering_state = cb;
    ctx->on_gathering_state_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    return handle->rtcSetGatheringStateChangeCallback(pc, cb ? ldc_gathering_state_bridge : NULL);
}

int32_t ldc_set_signaling_state_callback(
    ldc_wrapper_t *handle,
    int32_t pc,
    ldc_signaling_state_cb cb,
    void *user
) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_find_pc_ctx(handle, pc);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_INVALID;
    }
    ctx->on_signaling_state = cb;
    ctx->on_signaling_state_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    return handle->rtcSetSignalingStateChangeCallback(pc, cb ? ldc_signaling_state_bridge : NULL);
}

int32_t ldc_set_data_channel_callback(ldc_wrapper_t *handle, int32_t pc, ldc_data_channel_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_find_pc_ctx(handle, pc);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_INVALID;
    }
    ctx->on_data_channel = cb;
    ctx->on_data_channel_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    return handle->rtcSetDataChannelCallback(pc, cb ? ldc_data_channel_bridge : NULL);
}

int32_t ldc_set_track_callback(ldc_wrapper_t *handle, int32_t pc, ldc_track_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_pc_ctx *ctx = ldc_find_pc_ctx(handle, pc);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_INVALID;
    }
    ctx->on_track = cb;
    ctx->on_track_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(pc, ctx);
    return handle->rtcSetTrackCallback(pc, cb ? ldc_track_bridge : NULL);
}

int32_t ldc_set_local_description(ldc_wrapper_t *handle, int32_t pc, const char *type_or_null) {
    if (!handle) return RTC_ERR_INVALID;
    return handle->rtcSetLocalDescription(pc, type_or_null);
}

int32_t ldc_set_remote_description(ldc_wrapper_t *handle, int32_t pc, const char *sdp, const char *type) {
    if (!handle || !sdp || !type) return RTC_ERR_INVALID;
    return handle->rtcSetRemoteDescription(pc, sdp, type);
}

int32_t ldc_add_remote_candidate(ldc_wrapper_t *handle, int32_t pc, const char *candidate, const char *mid) {
    if (!handle || !candidate || !mid) return RTC_ERR_INVALID;
    return handle->rtcAddRemoteCandidate(pc, candidate, mid);
}

int32_t ldc_get_local_description(ldc_wrapper_t *handle, int32_t pc, uint8_t **out_ptr, size_t *out_len) {
    if (!handle) return RTC_ERR_INVALID;
    return ldc_read_rtc_string(handle->rtcGetLocalDescription, pc, out_ptr, out_len);
}

int32_t ldc_get_remote_description(ldc_wrapper_t *handle, int32_t pc, uint8_t **out_ptr, size_t *out_len) {
    if (!handle) return RTC_ERR_INVALID;
    return ldc_read_rtc_string(handle->rtcGetRemoteDescription, pc, out_ptr, out_len);
}

int32_t ldc_get_local_description_type(ldc_wrapper_t *handle, int32_t pc, uint8_t **out_ptr, size_t *out_len) {
    if (!handle) return RTC_ERR_INVALID;
    return ldc_read_rtc_string(handle->rtcGetLocalDescriptionType, pc, out_ptr, out_len);
}

int32_t ldc_get_remote_description_type(ldc_wrapper_t *handle, int32_t pc, uint8_t **out_ptr, size_t *out_len) {
    if (!handle) return RTC_ERR_INVALID;
    return ldc_read_rtc_string(handle->rtcGetRemoteDescriptionType, pc, out_ptr, out_len);
}

int32_t ldc_add_opus_track(
    ldc_wrapper_t *handle,
    int32_t pc,
    int32_t direction,
    uint32_t ssrc,
    int32_t payload_type,
    const char *mid,
    const char *name,
    const char *msid,
    const char *track_id,
    int32_t *out_track
) {
    if (!handle || !out_track) return RTC_ERR_INVALID;

    rtcTrackInit init;
    memset(&init, 0, sizeof(init));

    init.direction =
        (direction >= RTC_DIRECTION_UNKNOWN && direction <= RTC_DIRECTION_INACTIVE)
            ? (rtcDirection)direction
            : RTC_DIRECTION_SENDRECV;
    init.codec = RTC_CODEC_OPUS;
    init.payloadType = (payload_type > 0 && payload_type < 128) ? payload_type : 111;
    init.ssrc = (ssrc != 0) ? ssrc : 12345;
    init.mid = (mid && mid[0] != '\0') ? mid : "0";
    init.name = (name && name[0] != '\0') ? name : "audio";
    init.msid = (msid && msid[0] != '\0') ? msid : "stream";
    init.trackId = (track_id && track_id[0] != '\0') ? track_id : "audio-track";

    const int tr = handle->rtcAddTrackEx(pc, &init);
    if (tr < 0) return tr;

    *out_track = tr;
    return RTC_ERR_SUCCESS;
}

int32_t ldc_create_data_channel(ldc_wrapper_t *handle, int32_t pc, const char *label, int32_t *out_dc) {
    if (!handle || !out_dc) return RTC_ERR_INVALID;

    const char *safe_label = (label && label[0] != '\0') ? label : "events";
    const int dc = handle->rtcCreateDataChannel(pc, safe_label);
    if (dc < 0) return dc;

    *out_dc = dc;
    return RTC_ERR_SUCCESS;
}

int32_t ldc_create_data_channel_ex(
    ldc_wrapper_t *handle,
    int32_t pc,
    const char *label,
    int32_t unordered,
    int32_t unreliable,
    uint32_t max_packet_lifetime,
    uint32_t max_retransmits,
    int32_t negotiated,
    int32_t manual_stream,
    uint16_t stream,
    const char *protocol,
    int32_t *out_dc
) {
    if (!handle || !out_dc) return RTC_ERR_INVALID;

    const char *safe_label = (label && label[0] != '\0') ? label : "events";

    if (!handle->rtcCreateDataChannelEx) {
        if (unordered || unreliable || max_packet_lifetime || max_retransmits || negotiated || manual_stream || stream ||
            (protocol && protocol[0] != '\0')) {
            return RTC_ERR_NOT_AVAIL;
        }

        return ldc_create_data_channel(handle, pc, safe_label, out_dc);
    }

    rtcDataChannelInit init;
    memset(&init, 0, sizeof(init));

    init.reliability.unordered = unordered != 0;
    init.reliability.unreliable = unreliable != 0;
    init.reliability.maxPacketLifeTime = max_packet_lifetime;
    init.reliability.maxRetransmits = max_retransmits;
    init.negotiated = negotiated != 0;
    init.manualStream = manual_stream != 0;
    init.stream = stream;
    init.protocol = (protocol && protocol[0] != '\0') ? protocol : NULL;

    const int dc = handle->rtcCreateDataChannelEx(pc, safe_label, &init);
    if (dc < 0) return dc;

    *out_dc = dc;
    return RTC_ERR_SUCCESS;
}

int32_t ldc_set_open_callback(ldc_wrapper_t *handle, int32_t id, ldc_open_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_id_ctx *ctx = ldc_get_or_create_id_ctx(handle, id);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_FAILURE;
    }
    ctx->on_open = cb;
    ctx->on_open_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(id, ctx);
    return handle->rtcSetOpenCallback(id, cb ? ldc_open_bridge : NULL);
}

int32_t ldc_set_closed_callback(ldc_wrapper_t *handle, int32_t id, ldc_closed_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_id_ctx *ctx = ldc_get_or_create_id_ctx(handle, id);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_FAILURE;
    }
    ctx->on_closed = cb;
    ctx->on_closed_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(id, ctx);
    return handle->rtcSetClosedCallback(id, cb ? ldc_closed_bridge : NULL);
}

int32_t ldc_set_error_callback(ldc_wrapper_t *handle, int32_t id, ldc_error_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_id_ctx *ctx = ldc_get_or_create_id_ctx(handle, id);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_FAILURE;
    }
    ctx->on_error = cb;
    ctx->on_error_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(id, ctx);
    return handle->rtcSetErrorCallback(id, cb ? ldc_error_bridge : NULL);
}

int32_t ldc_set_message_callback(ldc_wrapper_t *handle, int32_t id, ldc_message_cb cb, void *user) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_id_ctx *ctx = ldc_get_or_create_id_ctx(handle, id);
    if (!ctx) {
        pthread_mutex_unlock(&handle->mu);
        return RTC_ERR_FAILURE;
    }
    ctx->on_message = cb;
    ctx->on_message_user = user;
    pthread_mutex_unlock(&handle->mu);

    handle->rtcSetUserPointer(id, ctx);
    return handle->rtcSetMessageCallback(id, cb ? ldc_message_bridge : NULL);
}

int32_t ldc_send_message(ldc_wrapper_t *handle, int32_t id, const uint8_t *data, size_t len) {
    if (!handle || (!data && len > 0) || len > (size_t)INT32_MAX) return RTC_ERR_INVALID;
    return handle->rtcSendMessage(id, (const char *)data, (int)len);
}

int32_t ldc_close_id(ldc_wrapper_t *handle, int32_t id) {
    if (!handle) return RTC_ERR_INVALID;
    return handle->rtcClose(id);
}

int32_t ldc_delete_id(ldc_wrapper_t *handle, int32_t id) {
    if (!handle) return RTC_ERR_INVALID;

    pthread_mutex_lock(&handle->mu);
    ldc_id_ctx *ctx = ldc_detach_id_ctx(handle, id);
    pthread_mutex_unlock(&handle->mu);

    if (ctx != NULL) {
        ldc_disable_id_callbacks(handle, id);
        handle->rtcSetUserPointer(id, NULL);
    }

    const int rc = handle->rtcDelete(id);

    if (ctx != NULL) {
        free(ctx);
    }

    return rc;
}

int32_t ldc_is_open(ldc_wrapper_t *handle, int32_t id) {
    if (!handle) return RTC_ERR_INVALID;
    return handle->rtcIsOpen(id) ? 1 : 0;
}

int32_t ldc_is_closed(ldc_wrapper_t *handle, int32_t id) {
    if (!handle) return RTC_ERR_INVALID;
    return handle->rtcIsClosed(id) ? 1 : 0;
}

int32_t ldc_free_buffer(uint8_t *ptr, size_t len) {
    (void)len;
    free(ptr);
    return RTC_ERR_SUCCESS;
}

const char *ldc_last_error(ldc_wrapper_t *handle) {
    if (!handle) return "invalid handle";
    return handle->last_error;
}
