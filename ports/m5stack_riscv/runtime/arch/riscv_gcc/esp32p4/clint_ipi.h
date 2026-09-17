/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2024 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)～(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 *
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 *
 *  $Id: gic_ipi.h 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *    プロセッサ間割込みに関する定義（CLINT用）
 */

#ifndef TOPPERS_CLINT_IPI_H
#define TOPPERS_CLINT_IPI_H

/*
 *  ESP32-P4 のハードウェア資源の定義
 */
#include "esp32p4.h"

/*
 *  プロセッサ間割込みに使用する割込みの番号
 */
#define IPINO_DISPATCH			UINT_C(0)		/* ディスパッチ要求 */
#define IPINO_EXT_KER			UINT_C(1)		/* カーネル終了要求 */
#define IPINO_SET_HRT_EVT		UINT_C(2)		/* 高分解能タイマ設定要求 */
#define IPINO_START_SCYC		UINT_C(3)		/* システム周期開始要求 */

#ifndef TOPPERS_MACRO_ONLY

/*
 *  msip アドレスは **コアローカル相対** で扱う(ESP32-P4 CLINT)．
 *    各コアから見て CLINT_BASE(0x2000_0000) が「自コア」の msip，
 *    +CLINT_CORE_STRIDE(0x2000_1000_0... =0x2001_0000) が「相手コア(peer)」の msip．
 *    (2コア固定．実機検証: core0 が 0x2001_0000 を書くと core1 が割込み，
 *     core1 が自分をクリアするには 0x2000_0000(self)．絶対pidオフセットだと
 *     core1 が自分の msip をクリアできず msip 割込み無限ループになる．)
 */
#define MSIP_SELF_ADDR   ((uint32_t *)(CLINT_BASE))
#define MSIP_PEER_ADDR   ((uint32_t *)(CLINT_BASE + CLINT_CORE_STRIDE))

/*
 *  Machine Software Interrupt の発生(prcid 宛)
 *    prcid が自コアなら self，他コアなら peer の msip をセット．
 */
Inline void
raise_msip(ID prcid)
{
    ID self;
    uint32_t *addr;

    sil_get_pid(&self);
    addr = (prcid == self) ? MSIP_SELF_ADDR : MSIP_PEER_ADDR;
    /*
     *  IPI 発行前のリリースバリア．
     *  呼出し側(update_schedtsk_dsp 等)は p_schedtsk/レディキュー等を更新して
     *  から本関数で msip を立てる．RISC-V の弱メモリ順序では，これらデータ
     *  更新の store と msip アクセスが他コアから逆順に見えうる．すると msip で
     *  起こされた対象コアが古い p_schedtsk(例: NULL)を読み，実行可能タスクが
     *  あるのに再びアイドルへ落ちて二度と起きない(lost wakeup)．実機確認:
     *  mtrans2 の他コア発 sus/rsm_tsk → アイドルコア起床が数千回叩かれ
     *  決定論的にデッドロックしていた．fence で先行 store を確定させる(対は
     *  clear_msip 側の acquire バリア)．
     *  下記は msip を読んでから条件付きで書く(合体)ため，後続に read も含める
     *  rw,rw とする(先行 store を msip 読出しより前に順序付ける．合体で write を
     *  省いた場合の lost-wakeup 防止に必須．後述)．
     */
    __asm__ volatile ("fence rw, rw" ::: "memory");
    /*
     *  送出側の msip マージ(coalesce)．
     *  msip は level-pending の1ビットで，対象コアが未クリア(=1)なら既に IPI が
     *  保留中である．その msi_handler は無条件に dispatch_handler を呼んで
     *  p_schedtsk を再評価する(要求の実体は p_schedtsk / 各 req フラグにあり，
     *  msip は起床信号にすぎない)．よって既に pending なら再 set は冗長で，対象
     *  コアの割込み回数も増えない．冗長な MMIO write/read-back を省くことで，
     *  storm 時の送出側コスト・コア間バス競合を下げる(FreeRTOS の pending-yield
     *  相当)．
     *
     *  lost-wakeup 無し: 上の fence(rw,rw)が先行 store を msip 読出しより前に順序
     *  付ける．pending を読んだ時点で対象コアは未クリアであり，その msip は対象
     *  コアが後で clear する．コヒーレンス順序(本コアの read < 対象コアの clear)と
     *  clear_msip 側 acquire により，先行 store は対象コアの p_schedtsk 読出しより
     *  前に観測される．すなわち合体で write を省いても，保留中 msip の処理が必ず
     *  最新の p_schedtsk を見てディスパッチする．
     */
    if (sil_rew_mem(addr) == 0U) {
        sil_swrw_mem(addr, 1U);
        (void)sil_rew_mem(addr);    /* 読戻しで write を押し出す */
    }
}

/*
 *  Machine Software Interrupt のクリア
 *    clear は常に自コア(msi_handler が p_my_pcb->prcid=自分 で呼ぶ)．
 *    コアローカルなので self(CLINT_BASE) を 0 にする．
 */
Inline void
clear_msip(ID prcid)
{
    (void) prcid;
    sil_swrw_mem(MSIP_SELF_ADDR, 0U);
    (void)sil_rew_mem(MSIP_SELF_ADDR);
    /*
     *  IPI 受信側のアクワイアバリア．
     *  msi_handler 入口で自コア msip をクリアした後，ディスパッチ判定で
     *  p_schedtsk 等の共有データを読む．先行コアが raise_msip 前に fence で
     *  確定させた p_schedtsk 更新を，本コアが確実に観測してから読むよう，
     *  ここで fence を張る(raise_msip 側 release との対)．
     */
    __asm__ volatile ("fence r, rw" ::: "memory");
}


/*
 *  MSI IPI 依存部
 */
#include "msi_ipi.h"

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CLINT_IPI_H */
