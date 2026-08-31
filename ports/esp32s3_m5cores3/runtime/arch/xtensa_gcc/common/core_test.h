/*
 *  テストプログラム用のコア依存定義（Xtensa用）
 *
 *  CPU例外テスト(test_cpuexc系)用の定義。RAISE_CPU_EXCEPTIONはXtensaの
 *  不正命令(ill)でIllegalInstruction例外(EXCCAUSE=0)を起こす。DEF_EXCで
 *  登録したハンドラはcore_exc_entry経由でディスパッチされる。
 */
#ifndef TOPPERS_CORE_TEST_H
#define TOPPERS_CORE_TEST_H

#define CPUEXC1_PRC1	((1 << 16) | 0)	/* EXCCAUSE=0 IllegalInstruction相当 */

#define RAISE_CPU_EXCEPTION	Asm("ill")

/*
 *  CPU例外ハンドラからの復帰準備（発生させた ill 命令を跳ばす）
 *
 *  Xtensaの ill は3バイト命令。CPU例外フレーム(T_EXCINF)の復帰PCを3進めて
 *  おくことで、ハンドラからの復帰時にillの次命令から再開する。これを定義
 *  しないテスト（test_cpuexc6）はpcを進めず、タスク切替でリカバリする。
 */
#define PREPARE_RETURN_CPUEXC	(((T_EXCINF *) p_excinf)->pc += 3)

#endif /* TOPPERS_CORE_TEST_H */
