#ifndef LIBDATACHANNEL_WRAPPER_H
#define LIBDATACHANNEL_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ldc_wrapper ldc_wrapper_t;

typedef void (*ldc_local_description_cb)(int32_t pc, const char *sdp, const char *type, void *user);
typedef void (*ldc_local_candidate_cb)(int32_t pc, const char *candidate, const char *mid, void *user);
typedef void (*ldc_state_cb)(int32_t pc, int32_t state, void *user);
typedef void (*ldc_ice_state_cb)(int32_t pc, int32_t state, void *user);
typedef void (*ldc_gathering_state_cb)(int32_t pc, int32_t state, void *user);
typedef void (*ldc_signaling_state_cb)(int32_t pc, int32_t state, void *user);
typedef void (*ldc_data_channel_cb)(int32_t pc, int32_t dc_id, void *user);
typedef void (*ldc_track_cb)(int32_t pc, int32_t track_id, void *user);
typedef void (*ldc_open_cb)(int32_t id, void *user);
typedef void (*ldc_closed_cb)(int32_t id, void *user);
typedef void (*ldc_error_cb)(int32_t id, const char *error, void *user);
typedef void (*ldc_message_cb)(int32_t id, const uint8_t *data, size_t len, void *user);

int32_t ldc_wrapper_create(ldc_wrapper_t **out_handle, const char *lib_path);
void ldc_wrapper_destroy(ldc_wrapper_t *handle);

int32_t ldc_create_peer_connection(ldc_wrapper_t *handle, const char *stun_url, int32_t *out_pc);
int32_t ldc_close_peer_connection(ldc_wrapper_t *handle, int32_t pc);
int32_t ldc_delete_peer_connection(ldc_wrapper_t *handle, int32_t pc);

int32_t ldc_set_local_description_callback(ldc_wrapper_t *handle, int32_t pc, ldc_local_description_cb cb, void *user);
int32_t ldc_set_local_candidate_callback(ldc_wrapper_t *handle, int32_t pc, ldc_local_candidate_cb cb, void *user);
int32_t ldc_set_state_callback(ldc_wrapper_t *handle, int32_t pc, ldc_state_cb cb, void *user);
int32_t ldc_set_ice_state_callback(ldc_wrapper_t *handle, int32_t pc, ldc_ice_state_cb cb, void *user);
int32_t ldc_set_gathering_state_callback(ldc_wrapper_t *handle, int32_t pc, ldc_gathering_state_cb cb, void *user);
int32_t ldc_set_signaling_state_callback(ldc_wrapper_t *handle, int32_t pc, ldc_signaling_state_cb cb, void *user);
int32_t ldc_set_data_channel_callback(ldc_wrapper_t *handle, int32_t pc, ldc_data_channel_cb cb, void *user);
int32_t ldc_set_track_callback(ldc_wrapper_t *handle, int32_t pc, ldc_track_cb cb, void *user);

int32_t ldc_set_local_description(ldc_wrapper_t *handle, int32_t pc, const char *type_or_null);
int32_t ldc_set_remote_description(ldc_wrapper_t *handle, int32_t pc, const char *sdp, const char *type);
int32_t ldc_add_remote_candidate(ldc_wrapper_t *handle, int32_t pc, const char *candidate, const char *mid);

int32_t ldc_get_local_description(ldc_wrapper_t *handle, int32_t pc, uint8_t **out_ptr, size_t *out_len);
int32_t ldc_get_remote_description(ldc_wrapper_t *handle, int32_t pc, uint8_t **out_ptr, size_t *out_len);
int32_t ldc_get_local_description_type(ldc_wrapper_t *handle, int32_t pc, uint8_t **out_ptr, size_t *out_len);
int32_t ldc_get_remote_description_type(ldc_wrapper_t *handle, int32_t pc, uint8_t **out_ptr, size_t *out_len);

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
);

int32_t ldc_create_data_channel(ldc_wrapper_t *handle, int32_t pc, const char *label, int32_t *out_dc);
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
);

int32_t ldc_set_open_callback(ldc_wrapper_t *handle, int32_t id, ldc_open_cb cb, void *user);
int32_t ldc_set_closed_callback(ldc_wrapper_t *handle, int32_t id, ldc_closed_cb cb, void *user);
int32_t ldc_set_error_callback(ldc_wrapper_t *handle, int32_t id, ldc_error_cb cb, void *user);
int32_t ldc_set_message_callback(ldc_wrapper_t *handle, int32_t id, ldc_message_cb cb, void *user);
int32_t ldc_send_message(ldc_wrapper_t *handle, int32_t id, const uint8_t *data, size_t len);
int32_t ldc_close_id(ldc_wrapper_t *handle, int32_t id);
int32_t ldc_delete_id(ldc_wrapper_t *handle, int32_t id);
int32_t ldc_is_open(ldc_wrapper_t *handle, int32_t id);
int32_t ldc_is_closed(ldc_wrapper_t *handle, int32_t id);

int32_t ldc_free_buffer(uint8_t *ptr, size_t len);
const char *ldc_last_error(ldc_wrapper_t *handle);

#ifdef __cplusplus
}
#endif

#endif
