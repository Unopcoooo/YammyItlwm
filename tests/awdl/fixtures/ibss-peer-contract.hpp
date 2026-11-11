#pragma once
#include <stdint.h>
#define __packed __attribute__((packed))
#define ETHER_ADDR_LEN 6
#define IWM_STA_LINK        0
#define IWM_STA_MODIFY_QUEUES            (1 << 7)
#define IWM_STA_FLG_FAT_EN_MSK        (3 << 26)
#define IWM_STA_FLG_MIMO_EN_MSK        (3 << 28)
#define IWM_STA_FLG_RTS_MIMO_PROT    (1 << 17)
#define IWM_NUM_OF_TBS        20
#define IWM_DEF_CMD_PAYLOAD_SIZE 320
#define IWM_TX_CMD        0x1c
#define IWM_TX_CMD_FLG_ACK        (1 << 3)
#define IWM_TX_CMD_FLG_SEQ_CTL        (1 << 13)
#define IWM_TX_CMD_FLG_MH_PAD        (1 << 20)
#define IWM_TX_CMD_OFFLD_PAD        (1 << 13)
#define IWM_TX_CMD_FLG_BT_PRIO_POS      11
#define IWM_TX_STATUS_MSK        0x000000ff
#define IWM_TX_STATUS_SUCCESS        0x01
#define IWM_TX_STATUS_DIRECT_DONE    0x02
struct iwm_add_sta_cmd {
    uint8_t add_modify;
    uint8_t awake_acs;
    uint16_t tid_disable_tx;
    uint32_t mac_id_n_color;
    uint8_t addr[ETHER_ADDR_LEN];    /* _STA_ID_MODIFY_INFO_API_S_VER_1 */
    uint16_t reserved2;
    uint8_t sta_id;
    uint8_t modify_mask;
    uint16_t reserved3;
    uint32_t station_flags;
    uint32_t station_flags_msk;
    uint8_t add_immediate_ba_tid;
    uint8_t remove_immediate_ba_tid;
    uint16_t add_immediate_ba_ssn;
    uint16_t sleep_tx_count;
    uint8_t sleep_state_flags;
    uint8_t station_type;
    uint16_t assoc_id;
    uint16_t beamform_flags;
    uint32_t tfd_queue_msk;
    uint16_t rx_ba_window;
    uint8_t sp_length;
    uint8_t uapsd_acs;
} __packed;
struct iwm_rm_sta_cmd {
    uint8_t sta_id;
    uint8_t reserved[3];
} __packed;
struct iwm_tfd_tb {
    uint32_t lo;
    uint16_t hi_n_len;
} __packed;
struct iwm_tfd {
    uint8_t __reserved1[3];
    uint8_t num_tbs;
    struct iwm_tfd_tb tbs[IWM_NUM_OF_TBS];
    uint32_t __pad;
} __packed;
struct iwm_tx_cmd {
    uint16_t len;
    uint16_t offload_assist;
    uint32_t tx_flags;
    struct {
        uint8_t try_cnt;
        uint8_t btkill_cnt;
        uint16_t reserved;
    } scratch; /* DRAM_SCRATCH_API_U_VER_1 */
    uint32_t rate_n_flags;
    uint8_t sta_id;
    uint8_t sec_ctl;
    uint8_t initial_rate_index;
    uint8_t reserved2;
    uint8_t key[16];
    uint16_t next_frame_flags;
    uint16_t reserved3;
    uint32_t life_time;
    uint32_t dram_lsb_ptr;
    uint8_t dram_msb_ptr;
    uint8_t rts_retry_limit;
    uint8_t data_retry_limit;
    uint8_t tid_tspec;
    uint16_t pm_frame_timeout;
    uint16_t driver_txop;
    uint8_t payload[0];

} __packed;
struct iwm_cmd_header {
    uint8_t code;
    uint8_t flags;
    uint8_t idx;
    uint8_t qid;
} __packed;
struct iwm_cmd_header_wide {
    uint8_t opcode;
    uint8_t group_id;
    uint8_t idx;
    uint8_t qid;
    uint16_t length;
    uint8_t reserved;
    uint8_t version;
} __packed;
struct iwm_device_cmd {
    union {
        struct {
            struct iwm_cmd_header hdr;
            uint8_t data[IWM_DEF_CMD_PAYLOAD_SIZE];
        };
        struct {
            struct iwm_cmd_header_wide hdr_wide;
            uint8_t data_wide[IWM_DEF_CMD_PAYLOAD_SIZE -
                    sizeof(struct iwm_cmd_header_wide) +
                    sizeof(struct iwm_cmd_header)];
        };
    };
} __packed;
static_assert(sizeof(iwm_add_sta_cmd)==48&&sizeof(iwm_rm_sta_cmd)==4&&sizeof(iwm_tfd)==128&&sizeof(iwm_tx_cmd)==56,"native wire ABI");
