struct iwm_agg_tx_status {
    uint16_t status;
    uint8_t idx;
    uint8_t qid;
} __packed;
struct iwm_tx_resp {
    uint8_t frame_count;
    uint8_t bt_kill_count;
    uint8_t failure_rts;
    uint8_t failure_frame;
    uint32_t initial_rate;
    uint16_t wireless_media_time;

    uint8_t pa_status;
    uint8_t pa_integ_res_a[3];
    uint8_t pa_integ_res_b[3];
    uint8_t pa_integ_res_c[3];
    uint16_t measurement_req_id;
    uint8_t reduced_tpc;
    uint8_t reserved;

    uint32_t tfd_info;
    uint16_t seq_ctl;
    uint16_t byte_cnt;
    uint8_t tlc_info;
    uint8_t ra_tid;
    uint16_t frame_ctrl;
    struct iwm_agg_tx_status status[];
} __packed;