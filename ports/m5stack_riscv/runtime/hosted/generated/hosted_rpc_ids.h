/*
 *  **自動生成。手で編集しない。**
 *
 *  生成器: cmake/a1_p4_rpc_proto_extract.py
 *  出典  : esp_hosted_rpc.pb-c.c / esp_hosted_rpc.pb-c.h
 *          （esp-hosted 2.12.9 の protobuf-c 生成物。読み取り専用）
 *
 *  段 7c は上流の RPC 実装も protobuf-c もリンクしない。要求 1 本ぶんの
 *  符号化を自前で書くため、**ID と field 番号だけ**をここへ機械抽出する。
 *  判定基準: .steering/20260816-p4-7c-rpc-wifi/AC.md §3-2
 */

#ifndef HOSTED_RPC_IDS_H
#define HOSTED_RPC_IDS_H

/*  Rpc envelope の field 番号（`Rpc` メッセージそのもの）  */
#define HOSTED_RPC_FIELD_MSG_TYPE     1      /* ENUM msg_type */
#define HOSTED_RPC_FIELD_MSG_ID       2      /* ENUM msg_id */
#define HOSTED_RPC_FIELD_UID          3      /* UINT32 uid */

/*  RpcType（msg_type の値）  */
#define HOSTED_RPC_TYPE_REQ        1      /* RPC_TYPE__Req */
#define HOSTED_RPC_TYPE_RESP       2      /* RPC_TYPE__Resp */
#define HOSTED_RPC_TYPE_EVENT      3      /* RPC_TYPE__Event */

/*  RpcId（msg_id の値）  */
#define HOSTED_RPC_ID_REQ_GET_COPROC_FWVER     350    /* RPC_ID__Req_GetCoprocessorFwVersion */
#define HOSTED_RPC_ID_RESP_GET_COPROC_FWVER    606    /* RPC_ID__Resp_GetCoprocessorFwVersion */
#define HOSTED_RPC_ID_REQ_GET_MAC              257    /* RPC_ID__Req_GetMACAddress */
#define HOSTED_RPC_ID_REQ_WIFI_DISCONNECT      283    /* RPC_ID__Req_WifiDisconnect */
#define HOSTED_RPC_ID_RESP_WIFI_DISCONNECT     539    /* RPC_ID__Resp_WifiDisconnect */
#define HOSTED_RPC_ID_RESP_GET_MAC             513    /* RPC_ID__Resp_GetMACAddress */
#define HOSTED_RPC_ID_REQ_GET_WIFI_MODE        259    /* RPC_ID__Req_GetWifiMode */
#define HOSTED_RPC_ID_RESP_GET_WIFI_MODE       515    /* RPC_ID__Resp_GetWifiMode */
#define HOSTED_RPC_ID_REQ_WIFI_INIT            278    /* RPC_ID__Req_WifiInit */
#define HOSTED_RPC_ID_RESP_WIFI_INIT           534    /* RPC_ID__Resp_WifiInit */
#define HOSTED_RPC_ID_REQ_SET_WIFI_MODE        260    /* RPC_ID__Req_SetWifiMode */
#define HOSTED_RPC_ID_RESP_SET_WIFI_MODE       516    /* RPC_ID__Resp_SetWifiMode */
#define HOSTED_RPC_ID_REQ_WIFI_START           280    /* RPC_ID__Req_WifiStart */
#define HOSTED_RPC_ID_RESP_WIFI_START          536    /* RPC_ID__Resp_WifiStart */
#define HOSTED_RPC_ID_REQ_SCAN_START           286    /* RPC_ID__Req_WifiScanStart */
#define HOSTED_RPC_ID_RESP_SCAN_START          542    /* RPC_ID__Resp_WifiScanStart */
#define HOSTED_RPC_ID_REQ_SCAN_AP_NUM          288    /* RPC_ID__Req_WifiScanGetApNum */
#define HOSTED_RPC_ID_RESP_SCAN_AP_NUM         544    /* RPC_ID__Resp_WifiScanGetApNum */
#define HOSTED_RPC_ID_REQ_SCAN_AP_RECORDS      289    /* RPC_ID__Req_WifiScanGetApRecords */
#define HOSTED_RPC_ID_RESP_SCAN_AP_RECORDS     545    /* RPC_ID__Resp_WifiScanGetApRecords */
#define HOSTED_RPC_ID_REQ_WIFI_SET_CONFIG      284    /* RPC_ID__Req_WifiSetConfig */
#define HOSTED_RPC_ID_RESP_WIFI_SET_CONFIG     540    /* RPC_ID__Resp_WifiSetConfig */
#define HOSTED_RPC_ID_REQ_WIFI_CONNECT         282    /* RPC_ID__Req_WifiConnect */
#define HOSTED_RPC_ID_RESP_WIFI_CONNECT        538    /* RPC_ID__Resp_WifiConnect */
#define HOSTED_RPC_ID_REQ_STA_GET_AP_INFO      294    /* RPC_ID__Req_WifiStaGetApInfo */
#define HOSTED_RPC_ID_RESP_STA_GET_AP_INFO     550    /* RPC_ID__Resp_WifiStaGetApInfo */
#define HOSTED_RPC_ID_EVENT_STA_CONNECTED      775    /* RPC_ID__Event_StaConnected */
#define HOSTED_RPC_ID_EVENT_STA_DISCONNECTED   776    /* RPC_ID__Event_StaDisconnected */

/*
 *  oneof payload の field 番号。
 *
 *  出典 `rpc_core.c:190` は `req.payload_case = (Rpc__PayloadCase) msg_id;`
 *  と書いている＝**payload の field 番号は msg_id そのもの**である。
 *  その不変条件を**機械が確かめられる**ように、両方を書き出して
 *  `_Static_assert` を並べる（下）。手で「同じはず」と決めない。
 */
#define HOSTED_RPC_PAYLOAD_REQ_GET_COPROC_FWVER     350    /* req_get_coprocessor_fwversion */
#define HOSTED_RPC_PAYLOAD_RESP_GET_COPROC_FWVER    606    /* resp_get_coprocessor_fwversion */
#define HOSTED_RPC_PAYLOAD_REQ_WIFI_DISCONNECT      283    /* req_wifi_disconnect */
#define HOSTED_RPC_PAYLOAD_RESP_WIFI_DISCONNECT     539    /* resp_wifi_disconnect */
#define HOSTED_RPC_PAYLOAD_REQ_GET_MAC              257    /* req_get_mac_address */
#define HOSTED_RPC_PAYLOAD_RESP_GET_MAC             513    /* resp_get_mac_address */
#define HOSTED_RPC_PAYLOAD_REQ_GET_WIFI_MODE        259    /* req_get_wifi_mode */
#define HOSTED_RPC_PAYLOAD_RESP_GET_WIFI_MODE       515    /* resp_get_wifi_mode */
#define HOSTED_RPC_PAYLOAD_REQ_WIFI_INIT            278    /* req_wifi_init */
#define HOSTED_RPC_PAYLOAD_RESP_WIFI_INIT           534    /* resp_wifi_init */
#define HOSTED_RPC_PAYLOAD_REQ_SET_WIFI_MODE        260    /* req_set_wifi_mode */
#define HOSTED_RPC_PAYLOAD_RESP_SET_WIFI_MODE       516    /* resp_set_wifi_mode */
#define HOSTED_RPC_PAYLOAD_REQ_WIFI_START           280    /* req_wifi_start */
#define HOSTED_RPC_PAYLOAD_RESP_WIFI_START          536    /* resp_wifi_start */
#define HOSTED_RPC_PAYLOAD_REQ_SCAN_START           286    /* req_wifi_scan_start */
#define HOSTED_RPC_PAYLOAD_RESP_SCAN_START          542    /* resp_wifi_scan_start */
#define HOSTED_RPC_PAYLOAD_REQ_SCAN_AP_NUM          288    /* req_wifi_scan_get_ap_num */
#define HOSTED_RPC_PAYLOAD_RESP_SCAN_AP_NUM         544    /* resp_wifi_scan_get_ap_num */
#define HOSTED_RPC_PAYLOAD_REQ_SCAN_AP_RECORDS      289    /* req_wifi_scan_get_ap_records */
#define HOSTED_RPC_PAYLOAD_RESP_SCAN_AP_RECORDS     545    /* resp_wifi_scan_get_ap_records */
#define HOSTED_RPC_PAYLOAD_REQ_WIFI_SET_CONFIG      284    /* req_wifi_set_config */
#define HOSTED_RPC_PAYLOAD_RESP_WIFI_SET_CONFIG     540    /* resp_wifi_set_config */
#define HOSTED_RPC_PAYLOAD_REQ_WIFI_CONNECT         282    /* req_wifi_connect */
#define HOSTED_RPC_PAYLOAD_RESP_WIFI_CONNECT        538    /* resp_wifi_connect */
#define HOSTED_RPC_PAYLOAD_REQ_STA_GET_AP_INFO      294    /* req_wifi_sta_get_ap_info */
#define HOSTED_RPC_PAYLOAD_RESP_STA_GET_AP_INFO     550    /* resp_wifi_sta_get_ap_info */

_Static_assert(HOSTED_RPC_PAYLOAD_REQ_GET_COPROC_FWVER == HOSTED_RPC_ID_REQ_GET_COPROC_FWVER,
               "payload field number != msg_id (req_get_coprocessor_fwversion)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_GET_COPROC_FWVER == HOSTED_RPC_ID_RESP_GET_COPROC_FWVER,
               "payload field number != msg_id (resp_get_coprocessor_fwversion)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_WIFI_DISCONNECT == HOSTED_RPC_ID_REQ_WIFI_DISCONNECT,
               "payload field number != msg_id (req_wifi_disconnect)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_WIFI_DISCONNECT == HOSTED_RPC_ID_RESP_WIFI_DISCONNECT,
               "payload field number != msg_id (resp_wifi_disconnect)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_GET_MAC == HOSTED_RPC_ID_REQ_GET_MAC,
               "payload field number != msg_id (req_get_mac_address)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_GET_MAC == HOSTED_RPC_ID_RESP_GET_MAC,
               "payload field number != msg_id (resp_get_mac_address)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_GET_WIFI_MODE == HOSTED_RPC_ID_REQ_GET_WIFI_MODE,
               "payload field number != msg_id (req_get_wifi_mode)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_GET_WIFI_MODE == HOSTED_RPC_ID_RESP_GET_WIFI_MODE,
               "payload field number != msg_id (resp_get_wifi_mode)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_WIFI_INIT == HOSTED_RPC_ID_REQ_WIFI_INIT,
               "payload field number != msg_id (req_wifi_init)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_WIFI_INIT == HOSTED_RPC_ID_RESP_WIFI_INIT,
               "payload field number != msg_id (resp_wifi_init)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_SET_WIFI_MODE == HOSTED_RPC_ID_REQ_SET_WIFI_MODE,
               "payload field number != msg_id (req_set_wifi_mode)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_SET_WIFI_MODE == HOSTED_RPC_ID_RESP_SET_WIFI_MODE,
               "payload field number != msg_id (resp_set_wifi_mode)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_WIFI_START == HOSTED_RPC_ID_REQ_WIFI_START,
               "payload field number != msg_id (req_wifi_start)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_WIFI_START == HOSTED_RPC_ID_RESP_WIFI_START,
               "payload field number != msg_id (resp_wifi_start)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_SCAN_START == HOSTED_RPC_ID_REQ_SCAN_START,
               "payload field number != msg_id (req_wifi_scan_start)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_SCAN_START == HOSTED_RPC_ID_RESP_SCAN_START,
               "payload field number != msg_id (resp_wifi_scan_start)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_SCAN_AP_NUM == HOSTED_RPC_ID_REQ_SCAN_AP_NUM,
               "payload field number != msg_id (req_wifi_scan_get_ap_num)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_SCAN_AP_NUM == HOSTED_RPC_ID_RESP_SCAN_AP_NUM,
               "payload field number != msg_id (resp_wifi_scan_get_ap_num)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_SCAN_AP_RECORDS == HOSTED_RPC_ID_REQ_SCAN_AP_RECORDS,
               "payload field number != msg_id (req_wifi_scan_get_ap_records)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_SCAN_AP_RECORDS == HOSTED_RPC_ID_RESP_SCAN_AP_RECORDS,
               "payload field number != msg_id (resp_wifi_scan_get_ap_records)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_WIFI_SET_CONFIG == HOSTED_RPC_ID_REQ_WIFI_SET_CONFIG,
               "payload field number != msg_id (req_wifi_set_config)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_WIFI_SET_CONFIG == HOSTED_RPC_ID_RESP_WIFI_SET_CONFIG,
               "payload field number != msg_id (resp_wifi_set_config)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_WIFI_CONNECT == HOSTED_RPC_ID_REQ_WIFI_CONNECT,
               "payload field number != msg_id (req_wifi_connect)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_WIFI_CONNECT == HOSTED_RPC_ID_RESP_WIFI_CONNECT,
               "payload field number != msg_id (resp_wifi_connect)");
_Static_assert(HOSTED_RPC_PAYLOAD_REQ_STA_GET_AP_INFO == HOSTED_RPC_ID_REQ_STA_GET_AP_INFO,
               "payload field number != msg_id (req_wifi_sta_get_ap_info)");
_Static_assert(HOSTED_RPC_PAYLOAD_RESP_STA_GET_AP_INFO == HOSTED_RPC_ID_RESP_STA_GET_AP_INFO,
               "payload field number != msg_id (resp_wifi_sta_get_ap_info)");

/*  応答メッセージの中身の field 番号  */
/*  rpc__resp__get_coprocessor_fw_version__field_descriptors  */
#define HOSTED_RPC_F_COPROC_FWVER_RESP           1    /* INT32 */
#define HOSTED_RPC_F_COPROC_FWVER_MAJOR1         2    /* UINT32 */
#define HOSTED_RPC_F_COPROC_FWVER_MINOR1         3    /* UINT32 */
#define HOSTED_RPC_F_COPROC_FWVER_PATCH1         4    /* UINT32 */
#define HOSTED_RPC_F_COPROC_FWVER_REVISION       5    /* INT32 */
#define HOSTED_RPC_F_COPROC_FWVER_PRERELEASE     6    /* INT32 */
#define HOSTED_RPC_F_COPROC_FWVER_BUILD          7    /* INT32 */
#define HOSTED_RPC_F_COPROC_FWVER_CHIP_ID        8    /* UINT32 */
#define HOSTED_RPC_F_COPROC_FWVER_IDF_TARGET     9    /* BYTES */

/*  rpc__resp__wifi_disconnect__field_descriptors  */
#define HOSTED_RPC_F_WIFI_DISCONNECT_RESP           1    /* INT32 */

/*  rpc__resp__get_mac_address__field_descriptors  */
#define HOSTED_RPC_F_GET_MAC_MAC            1    /* BYTES */
#define HOSTED_RPC_F_GET_MAC_RESP           2    /* INT32 */

/*  rpc__req__get_mac_address__field_descriptors  */
#define HOSTED_RPC_F_GET_MAC_REQ_MODE           1    /* INT32 */

/*  rpc__resp__get_mode__field_descriptors  */
#define HOSTED_RPC_F_GET_MODE_MODE           1    /* INT32 */
#define HOSTED_RPC_F_GET_MODE_RESP           2    /* INT32 */

/*  rpc__req__set_mode__field_descriptors  */
#define HOSTED_RPC_F_SET_MODE_REQ_MODE           1    /* INT32 */

/*  rpc__req__wifi_init__field_descriptors  */
#define HOSTED_RPC_F_WIFI_INIT_REQ_CFG            1    /* MESSAGE */

/*  wifi_init_config__field_descriptors  */
#define HOSTED_RPC_F_INITCFG_STATIC_RX_BUF_NUM 1    /* INT32 */
#define HOSTED_RPC_F_INITCFG_DYNAMIC_RX_BUF_NUM 2    /* INT32 */
#define HOSTED_RPC_F_INITCFG_TX_BUF_TYPE    3    /* INT32 */
#define HOSTED_RPC_F_INITCFG_STATIC_TX_BUF_NUM 4    /* INT32 */
#define HOSTED_RPC_F_INITCFG_DYNAMIC_TX_BUF_NUM 5    /* INT32 */
#define HOSTED_RPC_F_INITCFG_CACHE_TX_BUF_NUM 6    /* INT32 */
#define HOSTED_RPC_F_INITCFG_CSI_ENABLE     7    /* INT32 */
#define HOSTED_RPC_F_INITCFG_AMPDU_RX_ENABLE 8    /* INT32 */
#define HOSTED_RPC_F_INITCFG_AMPDU_TX_ENABLE 9    /* INT32 */
#define HOSTED_RPC_F_INITCFG_AMSDU_TX_ENABLE 10   /* INT32 */
#define HOSTED_RPC_F_INITCFG_NVS_ENABLE     11   /* INT32 */
#define HOSTED_RPC_F_INITCFG_NANO_ENABLE    12   /* INT32 */
#define HOSTED_RPC_F_INITCFG_RX_BA_WIN      13   /* INT32 */
#define HOSTED_RPC_F_INITCFG_WIFI_TASK_CORE_ID 14   /* INT32 */
#define HOSTED_RPC_F_INITCFG_BEACON_MAX_LEN 15   /* INT32 */
#define HOSTED_RPC_F_INITCFG_MGMT_SBUF_NUM  16   /* INT32 */
#define HOSTED_RPC_F_INITCFG_FEATURE_CAPS   17   /* UINT64 */
#define HOSTED_RPC_F_INITCFG_STA_DISCONNECTED_PM 18   /* BOOL */
#define HOSTED_RPC_F_INITCFG_ESPNOW_MAX_ENCRYPT_NUM 19   /* INT32 */
#define HOSTED_RPC_F_INITCFG_MAGIC          20   /* INT32 */
#define HOSTED_RPC_F_INITCFG_RX_MGMT_BUF_TYPE 21   /* INT32 */
#define HOSTED_RPC_F_INITCFG_RX_MGMT_BUF_NUM 22   /* INT32 */
#define HOSTED_RPC_F_INITCFG_TX_HETB_QUEUE_NUM 23   /* INT32 */
#define HOSTED_RPC_F_INITCFG_DUMP_HESIGB_ENABLE 24   /* INT32 */

/*  rpc__req__wifi_scan_start__field_descriptors  */
#define HOSTED_RPC_F_SCAN_START_REQ_CONFIG         1    /* MESSAGE */
#define HOSTED_RPC_F_SCAN_START_REQ_BLOCK          2    /* BOOL */
#define HOSTED_RPC_F_SCAN_START_REQ_CONFIG_SET     3    /* INT32 */

/*  rpc__resp__wifi_scan_get_ap_num__field_descriptors  */
#define HOSTED_RPC_F_AP_NUM_RESP           1    /* INT32 */
#define HOSTED_RPC_F_AP_NUM_NUMBER         2    /* INT32 */

/*  rpc__req__wifi_scan_get_ap_records__field_descriptors  */
#define HOSTED_RPC_F_AP_RECS_REQ_NUMBER         1    /* INT32 */

/*  rpc__resp__wifi_scan_get_ap_records__field_descriptors  */
#define HOSTED_RPC_F_AP_RECS_RESP           1    /* INT32 */
#define HOSTED_RPC_F_AP_RECS_NUMBER         2    /* INT32 */
#define HOSTED_RPC_F_AP_RECS_AP_RECORDS     3    /* MESSAGE */

/*  wifi_ap_record__field_descriptors  */
#define HOSTED_RPC_F_APREC_BSSID          1    /* BYTES */
#define HOSTED_RPC_F_APREC_SSID           2    /* BYTES */
#define HOSTED_RPC_F_APREC_PRIMARY        3    /* UINT32 */
#define HOSTED_RPC_F_APREC_SECOND         4    /* INT32 */
#define HOSTED_RPC_F_APREC_RSSI           5    /* INT32 */
#define HOSTED_RPC_F_APREC_AUTHMODE       6    /* INT32 */
#define HOSTED_RPC_F_APREC_PAIRWISE_CIPHER 7    /* INT32 */
#define HOSTED_RPC_F_APREC_GROUP_CIPHER   8    /* INT32 */
#define HOSTED_RPC_F_APREC_ANT            9    /* INT32 */
#define HOSTED_RPC_F_APREC_BITMASK        10   /* UINT32 */
#define HOSTED_RPC_F_APREC_COUNTRY        11   /* MESSAGE */
#define HOSTED_RPC_F_APREC_HE_AP          12   /* MESSAGE */
#define HOSTED_RPC_F_APREC_BANDWIDTH      13   /* UINT32 */
#define HOSTED_RPC_F_APREC_VHT_CH_FREQ1   14   /* UINT32 */
#define HOSTED_RPC_F_APREC_VHT_CH_FREQ2   15   /* UINT32 */

/*  rpc__req__wifi_set_config__field_descriptors  */
#define HOSTED_RPC_F_SETCFG_REQ_IFACE          1    /* INT32 */
#define HOSTED_RPC_F_SETCFG_REQ_CFG            2    /* MESSAGE */

/*  wifi_config__field_descriptors  */
#define HOSTED_RPC_F_WIFICFG_AP             1    /* MESSAGE */
#define HOSTED_RPC_F_WIFICFG_STA            2    /* MESSAGE */

/*  wifi_sta_config__field_descriptors  */
#define HOSTED_RPC_F_STACFG_SSID           1    /* BYTES */
#define HOSTED_RPC_F_STACFG_PASSWORD       2    /* BYTES */
#define HOSTED_RPC_F_STACFG_SCAN_METHOD    3    /* INT32 */
#define HOSTED_RPC_F_STACFG_BSSID_SET      4    /* BOOL */
#define HOSTED_RPC_F_STACFG_BSSID          5    /* BYTES */
#define HOSTED_RPC_F_STACFG_CHANNEL        6    /* UINT32 */
#define HOSTED_RPC_F_STACFG_LISTEN_INTERVAL 7    /* UINT32 */
#define HOSTED_RPC_F_STACFG_SORT_METHOD    8    /* INT32 */
#define HOSTED_RPC_F_STACFG_THRESHOLD      9    /* MESSAGE */
#define HOSTED_RPC_F_STACFG_PMF_CFG        10   /* MESSAGE */
#define HOSTED_RPC_F_STACFG_BITMASK        11   /* UINT32 */
#define HOSTED_RPC_F_STACFG_SAE_PWE_H2E    12   /* INT32 */
#define HOSTED_RPC_F_STACFG_FAILURE_RETRY_CNT 13   /* UINT32 */
#define HOSTED_RPC_F_STACFG_HE_BITMASK     14   /* UINT32 */
#define HOSTED_RPC_F_STACFG_SAE_H2E_IDENTIFIER 15   /* BYTES */
#define HOSTED_RPC_F_STACFG_SAE_PK_MODE    16   /* UINT32 */

/*  rpc__resp__wifi_sta_get_ap_info__field_descriptors  */
#define HOSTED_RPC_F_AP_INFO_RESP           1    /* INT32 */
#define HOSTED_RPC_F_AP_INFO_AP_RECORD      2    /* MESSAGE */

/*  rpc__event__sta_disconnected__field_descriptors  */
#define HOSTED_RPC_F_EV_DISC_RESP           1    /* INT32 */
#define HOSTED_RPC_F_EV_DISC_STA_DISCONNECTED 2    /* MESSAGE */

/*  wifi_event_sta_disconnected__field_descriptors  */
#define HOSTED_RPC_F_DISC_SSID           1    /* BYTES */
#define HOSTED_RPC_F_DISC_SSID_LEN       2    /* UINT32 */
#define HOSTED_RPC_F_DISC_BSSID          3    /* BYTES */
#define HOSTED_RPC_F_DISC_REASON         4    /* UINT32 */
#define HOSTED_RPC_F_DISC_RSSI           5    /* INT32 */

/*  rpc__resp__wifi_init__field_descriptors  */
#define HOSTED_RPC_F_RESP_WIFI_INIT_RESP           1    /* INT32 */

/*
 *  出典の総数（構造が変わったことに気づくための目印）。
 *  **判定に使う数ではない**——出典が増えても本段の符号化は変わらない。
 *  ここが動いたら「出典の版が変わった」と読む。
 */
#define HOSTED_RPC_ENVELOPE_NFIELD   246
#define HOSTED_RPC_NUM_RPCID         304

#endif /* HOSTED_RPC_IDS_H */
