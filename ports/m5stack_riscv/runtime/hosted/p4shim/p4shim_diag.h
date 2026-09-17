/*
 *  seam 版 Ethernet 診断タスクの定数と入口（段 E-seam・2026-08-15）
 *
 *  `.cfg` は cfg_py のプリプロセッサを通るが `#define` は書けないので、
 *  定数はこのヘッダに置いて `.cfg` と `.c` の**両方から読む**
 *  （C-1 の `esp_shim_intr_clic_lines.h` と同じ「単一真実源」の作法）。
 */
#ifndef ESP_P4SHIM_DIAG_H
#define ESP_P4SHIM_DIAG_H

/*
 *  優先度は**最低**にする。測ることで測られる側を乱さないため。
 *  eth 側は tcpip_thread=9 / RX=11 / rxpoll=12 / timer=13 を使う。
 */
#define P4SHIM_DIAG_PRI			14
#define P4SHIM_DIAG_STACK		2048

#ifndef TOPPERS_MACRO_ONLY
extern void	p4shim_diag_task(intptr_t exinf);
#endif

#endif /* ESP_P4SHIM_DIAG_H */
