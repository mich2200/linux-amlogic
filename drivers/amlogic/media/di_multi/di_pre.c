// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * drivers/amlogic/media/di_multi/di_pre.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/seq_file.h>

#include <linux/amlogic/media/vfm/vframe.h>
#include "deinterlace.h"
#include "deinterlace_dbg.h"

#include "di_data_l.h"
#include "di_data.h"
#include "di_dbg.h"
#include "di_vframe.h"
#include "di_que.h"
#include "di_task.h"

#include "di_prc.h"
#include "di_pre.h"

#include "nr_downscale.h"
#include "register.h"

/****************************************
 * 1. copy curr to last
 * 2. set curr
 ****************************************/
void pre_vinfo_set(unsigned int ch,
		   struct vframe_s *ori_vframe)
{
	struct di_hpre_s  *pre = get_hw_pre();

	struct di_vinfo_s *vc = &pre->vinf_curr;
	struct di_vinfo_s *vl = &pre->vinf_lst;

	memcpy(vl, vc, sizeof(struct di_vinfo_s));

	vc->ch = ch;
	vc->vtype = ori_vframe->type;
	vc->src_type = ori_vframe->source_type;
	vc->trans_fmt = ori_vframe->trans_fmt;

	if (COM_ME(ori_vframe->type, VIDTYPE_COMPRESS)) {
		vc->h = ori_vframe->compWidth;
		vc->v = ori_vframe->compHeight;
	} else {
		vc->h = ori_vframe->width;
		vc->v = ori_vframe->height;
	}
}

/****************************************
 * compare current vframe info with last
 * return
 *	0. no change
 *	1. video format change
 *	2. scan mode change?
 ****************************************/
unsigned int is_vinfo_change(unsigned int ch)
{
	struct di_hpre_s  *pre = get_hw_pre();

	struct di_vinfo_s *vc = &pre->vinf_curr;
	struct di_vinfo_s *vl = &pre->vinf_lst;
	struct di_pre_stru_s *ppre = get_pre_stru(ch);
	unsigned int ret = 0;

	if (vc->src_type != vl->src_type			||
	    !COM_M(DI_VFM_T_MASK_CHANGE, vc->vtype, vl->vtype)	||
	    vc->v != vl->v					||
	    vc->h != vl->h					||
	    vc->trans_fmt != vl->trans_fmt) {
		/* video format changed */
		ret = 1;
	} else if (!COM_M(VIDTYPE_VIU_FIELD, vc->vtype, vl->vtype))
		/* just scan mode changed */
		ret = 2;

	if (ret) {
		dim_print("%s:ch[%d]:%dth scchg2:0x%x/%d/%d/%d>0x%x/%d/%d/%d\n",
			  __func__,
			  ch,
			  /*jiffies_to_msecs(jiffies_64),*/
			  ppre->in_seq,
			  vl->vtype,
			  vl->h,
			  vl->v,
			  vl->src_type,
			  vc->vtype,
			  vc->h,
			  vc->v,
			  vc->src_type);
	}

	return ret;
}

bool dim_bypass_detect(unsigned int ch, struct vframe_s *vfm)
{
	bool ret = false;

	if (!vfm)
		return ret;
	pre_vinfo_set(ch, vfm);
	if (is_vinfo_change(ch)) {
		if (!is_bypass2(vfm, ch)) {
			set_bypass2_complete(ch, false);
			PR_INF("%s:\n", __func__);
			/*task_send_ready();*/
			task_send_cmd(LCMD1(ECMD_CHG, ch));
			ret = true;
		}
	}
	return ret;
}

unsigned int di_get_other_ch(unsigned int curr)
{
	return curr ? 0 : 1;
}

bool is_bypass_i_p(void)
{
	bool ret = false;
	struct di_hpre_s  *pre = get_hw_pre();
	struct di_vinfo_s *vc = &pre->vinf_curr;
	#ifdef MARK_HIS
	struct di_vinfo_s *vl = &pre->vinf_lst;

	if (vl->ch != vc->ch			&&
	    VFMT_IS_I(vl->vtype)	&&
	    VFMT_IS_P(vc->vtype)) {
		ret = true;
	}
	#else
	unsigned int ch_c, ch_l;

	struct di_pre_stru_s *ppre_c, *ppre_l;

	if (!get_reg_flag(0)	||
	    !get_reg_flag(1))
		return ret;

	ch_c = vc->ch;
	ch_l = (ch_c ? 0 : 1);
	ppre_c = get_pre_stru(ch_c);
	ppre_l = get_pre_stru(ch_l);
	if (VFMT_IS_I(ppre_l->cur_inp_type)	&&
	    VFMT_IS_P(ppre_c->cur_inp_type)) {
		ret = true;
		dim_print("ch[%d]:bypass p\n", ch_c);
	}
	#endif

	return ret;
}

void dpre_clear(void)
{
	struct di_hpre_s  *pre = get_hw_pre();

	memset(pre, 0, sizeof(struct di_hpre_s));
}

static unsigned int dim_pre_tm_thd = 40;
module_param_named(dim_pre_tm_thd, dim_pre_tm_thd, uint, 0664);

void dpre_init(void)
{/*reg:*/
	struct di_hpre_s  *pre = get_hw_pre();

	pre->pre_st = EDI_PRE_ST4_IDLE;
	//bset(&pre->self_trig_mask, 2);//

	/*timer out*/
	di_tout_int(&pre->tout, dim_pre_tm_thd);	/*ms*/
}

void pw_use_hw_pre(enum EDI_SUB_ID channel, bool on)
{
	struct di_hpre_s  *pre = get_hw_pre();

	pre->hw_flg_busy_pre = on;
	if (on)
		pre->curr_ch = channel;
}

enum EDI_SUB_ID pw_ch_next_count(enum EDI_SUB_ID channel)
{
	int i;
	unsigned int lch, nch;

	nch = channel;
	for (i = 0; i < DI_CHANNEL_NUB; i++) {
		lch = channel + i + 1;
		if (lch >= DI_CHANNEL_NUB)
			lch -= DI_CHANNEL_NUB;

		if (get_reg_flag(lch)		&&
		    !get_flag_trig_unreg(lch)	&&
		    !is_bypss2_complete(lch)) {
			nch = lch;
			break;
		}
	}
	return nch;
}

/****************************************/
static bool pw_try_sw_ch_next_pre(enum EDI_SUB_ID channel)
{
	bool ret = false;
	struct di_hpre_s  *pre = get_hw_pre();
	enum EDI_SUB_ID lst_ch, nch;

	lst_ch = channel;

	nch = pw_ch_next_count(lst_ch);
	if (!get_reg_flag(nch)		||
	    get_flag_trig_unreg(nch)	||
	    is_bypss2_complete(nch))
		return false;

	pre->curr_ch = nch;
	pre->hw_flg_busy_pre = true;
	ret = true;

	/*dim_print("%s:%d->%d:%d\n", __func__, lst_ch, nch, ret);*/
	return ret;
}

/*****************************/
/* debug */
/*****************************/

unsigned int di_dbg_pre_cnt;

void dbg_cnt_begin(void)
{
	di_dbg_pre_cnt = 0x10;
}

void dbg_cnt_print(void)
{
	if (di_dbg_pre_cnt < 0xf)
		return;

	if (di_dbg_pre_cnt > 0x10) {
		di_dbg_pre_cnt++;
		pr_info("di:[%d]\n", di_dbg_pre_cnt);
	}

	if (di_dbg_pre_cnt > 0x15)
		di_dbg_pre_cnt = 0;
}

/*****************************/
/* STEP */
/*****************************/
void dpre_recyc(unsigned int ch)
{
	struct di_hpre_s  *pre = get_hw_pre();

	pre->check_recycle_buf_cnt = 0;
	while (dim_check_recycle_buf(ch) & 1) {
		if (pre->check_recycle_buf_cnt++ > MAX_IN_BUF_NUM) {
			di_pr_info("%s: dim_check_recycle_buf time out!!\n",
				   __func__);
			break;
		}
	}
}

void dpre_vdoing(unsigned int ch)
{
	struct di_post_stru_s *ppost = get_post_stru(ch);

	ppost->di_post_process_cnt = 0;
	while (dim_process_post_vframe(ch)) {
		if (ppost->di_post_process_cnt++ >
			MAX_POST_BUF_NUM) {
			di_pr_info("%s: dim_process_post_vframe time out!!\n",
				   __func__);
			break;
		}
	}
}

bool dpre_can_exit(unsigned int ch)
{
	struct di_hpre_s  *pre = get_hw_pre();
	bool ret = false;

	if (ch != pre->curr_ch) {
		ret = true;
	} else {
		if (pre->pre_st <= EDI_PRE_ST4_IDLE)
			ret = true;
	}
	if (!ret)
		PR_INF("%s:ch[%d]:curr[%d]:stat[%s] ret[%d]\n",
		       __func__,
		       ch, pre->curr_ch,
		       dpre_state4_name_get(pre->pre_st),
		       ret);
	return ret;
}

void dpre_dbg_f_trig(unsigned int cmd)
{
	struct di_task *tsk = get_task();

	struct di_hpre_s  *pre = get_hw_pre();

	if (down_interruptible(&tsk->sem)) {
		PR_ERR("%s:can't get sem\n", __func__);
		return;
	}

	/*set on/off and trig*/
	if (cmd & 0x10) {
		pre->dbg_f_en = 1;
		pre->dbg_f_cnt = cmd & 0xf;
		pre->dbg_f_lstate = pre->pre_st;
	} else {
		pre->dbg_f_en = 0;
	}

	up(&tsk->sem);
}

void dpre_process(void)
{
	bool reflesh;
	struct di_hpre_s  *pre = get_hw_pre();

	if (pre->dbg_f_en) {
		if (pre->dbg_f_cnt) {
			dpre_process_step4();
			pre->dbg_f_cnt--;
		}
		if (pre->dbg_f_lstate != pre->pre_st) {
			pr_info("ch[%d]:state:%s->%s\n",
				pre->curr_ch,
				dpre_state4_name_get(pre->dbg_f_lstate),
				dpre_state4_name_get(pre->pre_st));

			pre->dbg_f_lstate = pre->pre_st;
		}
		return;
	}

	reflesh = true;

	while (reflesh) {
		reflesh = dpre_process_step4();
		#ifdef MARK_HIS	/*debug only*/
		dbg_tsk("ch[%d]:st[%s]r[%d]\n", pre->curr_ch,
			dpre_state4_name_get(pre->pre_st), reflesh);
		#endif
	}
}

enum EDI_PRE_MT {
	EDI_PRE_MT_CHECK = K_DO_TABLE_ID_START,
	EDI_PRE_MT_SET,
	EDI_PRE_MT_WAIT_INT,
	EDI_PRE_MT_TIME_OUT,
};

/*use do_table:*/
unsigned int dpre_mtotal_check(void *data)
{
	struct di_hpre_s  *pre = get_hw_pre();
	unsigned int ret = K_DO_R_JUMP(K_DO_TABLE_ID_STOP);//K_DO_R_NOT_FINISH;
	unsigned int cfg_val;
	struct di_ch_s *pch;

	dbg_src_change_simple(pre->curr_ch);/*dbg only*/

	pch = get_chdata(pre->curr_ch);
	cfg_val = dim_pre_bypass(pch);
	if (cfg_val) {
		dbg_dt("cfg fail:bypass%d\n", cfg_val);
		cfg_val = dim_pre_de_buf_config(pre->curr_ch);
	} else {
		cfg_val = 70;
	}
	if (!cfg_val) {/*ok*/
		ret = K_DO_R_FINISH;
	} else {
		dbg_dt("cfg fail:%d\n", cfg_val);
		ret = K_DO_R_JUMP(K_DO_TABLE_ID_STOP);
	}

	return ret;
}

unsigned int dpre_mtotal_set(void *data)
{
	struct di_hpre_s  *pre = get_hw_pre();
//ary 2020-12-09	ulong flags = 0;
	struct di_ch_s *pch;

	pch = get_chdata(pre->curr_ch);

	/*dim_print("%s:\n", __func__);*/
	//trace_printk("%s\n", __func__);
//ary 2020-12-09	spin_lock_irqsave(&plist_lock, flags);
	dim_pre_de_process(pre->curr_ch);
	pre->irq_nr = false;
//ary 2020-12-09	spin_unlock_irqrestore(&plist_lock, flags);
	/*begin to count timer*/
	di_tout_contr(EDI_TOUT_CONTR_EN, &pre->tout);
	sct_polling(pch, 3);

	return K_DO_R_FINISH;
}

enum EDI_WAIT_INT {
	EDI_WAIT_INT_NEED_WAIT,
	EDI_WAIT_INT_HAVE_INT,
	EDI_WAIT_INT_TIME_OUT,
};

/*
 *return:	enum EDI_WAIT_INT
 *
 */
enum EDI_WAIT_INT di_pre_wait_int(void *data)
{
	struct di_hpre_s  *pre = get_hw_pre();
	//tmp ulong flags = 0;
	struct di_pre_stru_s *ppre;

	enum EDI_WAIT_INT ret = EDI_WAIT_INT_NEED_WAIT;

	if (pre->flg_int_done) {
		/*have INT done flg*/
		/*DI_INTR_CTRL[bit 0], NRWR_done, set by
		 * hardware when NRWR is done,clear by write 1
		 * by code;[bit 1]
		 * MTNWR_done, set by hardware when MTNWR
		 * is done, clear by write 1 by code;these two
		 * bits have nothing to do with
		 * DI_INTR_CTRL[16](NRW irq mask, 0 to enable
		 * irq) and DI_INTR_CTRL[17]
		 * (MTN irq mask, 0 to enable irq).two
		 * interrupts are raised if both
		 * DI_INTR_CTRL[16] and DI_INTR_CTRL[17] are 0
		 */

		/*di_pre_wait_irq_set(false);*/
		/*finish to count timer*/
		di_tout_contr(EDI_TOUT_CONTR_FINISH, &pre->tout);
		//tmp spin_lock_irqsave(&plist_lock, flags);

		dim_pre_de_done_buf_config(pre->curr_ch, false);

		pre->flg_int_done = 0;

		dpre_recyc(pre->curr_ch);
		dpre_vdoing(pre->curr_ch);

		//tmp spin_unlock_irqrestore(&plist_lock, flags);

		ppre = get_pre_stru(pre->curr_ch);

		ret = EDI_WAIT_INT_HAVE_INT;

	} else {
		/*check if timeout:*/
		if (di_tout_contr(EDI_TOUT_CONTR_CHECK, &pre->tout)) {
			/*di_pre_wait_irq_set(false);*/
			if (!atomic_dec_and_test(&get_hw_pre()->flg_wait_int)) {
				PR_WARN("%s:ch[%d]timeout\n", __func__,
					pre->curr_ch);
				di_tout_contr(EDI_TOUT_CONTR_EN, &pre->tout);
			} else {
				/*return K_DO_R_FINISH;*/
				ret = EDI_WAIT_INT_TIME_OUT;
			}
		}
	}
	/*debug:*/
	if (dbg_first_cnt_pre)
		dbg_first_frame("ch[%d],w_int[%d]\n", pre->curr_ch, ret);

	return ret;
}

unsigned int dpre_mtotal_wait_int(void *data)
{
	enum EDI_WAIT_INT wret;
	unsigned int ret = K_DO_R_NOT_FINISH;

	wret = di_pre_wait_int(NULL);
	switch (wret) {
	case EDI_WAIT_INT_NEED_WAIT:
		ret = K_DO_R_NOT_FINISH;
		break;
	case EDI_WAIT_INT_HAVE_INT:
		ret = K_DO_R_JUMP(K_DO_TABLE_ID_STOP);
		break;
	case EDI_WAIT_INT_TIME_OUT:
		ret = K_DO_R_FINISH;
		break;
	}
	return ret;
}

void dpre_mtotal_timeout_contr(void)
{
	struct di_hpre_s  *pre = get_hw_pre();

	/*move from di_pre_trigger_work*/
	if (dimp_get(edi_mp_di_dbg_mask) & 4)
		dim_dump_mif_size_state(pre->pres, pre->psts);
	hpre_timeout_read();

	if (DIM_IS_IC_EF(SC2))
		opl1()->pre_gl_sw(false);

	dimh_enable_di_pre_mif(false, dimp_get(edi_mp_mcpre_en));
	dcntr_dis();
	if (di_get_dts_nrds_en())
		dim_nr_ds_hw_ctrl(false);
	pre->pres->pre_de_irq_timeout_count++;

	pre->pres->pre_de_busy = 0;
	pre->pres->pre_de_clear_flag = 2;
	if ((dimp_get(edi_mp_di_dbg_mask) & 0x2)) {
		PR_WARN("DI:ch[%d]*****wait %d timeout 0x%x(%d ms)*****[%d]\n",
			pre->curr_ch,
			pre->pres->field_count_for_cont,
			RD(DI_INTR_CTRL),
			(unsigned int)((cur_to_usecs() -
			pre->pres->irq_time[0]) >> 10),
			pre->irq_nr);
	}
	/*******************************/
	dim_pre_de_done_buf_config(pre->curr_ch, true);

	dpre_recyc(pre->curr_ch);
	dpre_vdoing(pre->curr_ch);
	/*******************************/
	/*dpre_recyc(pre->curr_ch);*/
}

unsigned int dpre_mtotal_timeout(void *data)
{
//ary 2020-12-09	ulong flags = 0;

	//tmp spin_lock_irqsave(&plist_lock, flags);
	dpre_mtotal_timeout_contr();
	//tmp spin_unlock_irqrestore(&plist_lock, flags);

	return K_DO_R_JUMP(K_DO_TABLE_ID_STOP);
}

const struct do_table_ops_s pr_mode_total[] = {
	/*fix*/
	[K_DO_TABLE_ID_PAUSE] = {
	.id = K_DO_TABLE_ID_PAUSE,
	.mark = 0,
	.con = NULL,
	.do_op = NULL,
	.do_stop_op = NULL,
	.name = "pause",
	},
	[K_DO_TABLE_ID_STOP] = {
	.id = K_DO_TABLE_ID_STOP,
	.mark = 0,
	.con = NULL,
	.do_op = NULL,
	.do_stop_op = NULL,
	.name = "stop",
	},
	/******************/
	[K_DO_TABLE_ID_START] = {	/*EDI_PRE_MT_CHECK*/
	.id = K_DO_TABLE_ID_START,
	.mark = 0,
	.con = NULL,
	.do_op = dpre_mtotal_check,
	.do_stop_op = NULL,
	.name = "start-check_t",
	},
	[EDI_PRE_MT_SET] = {
	.id = EDI_PRE_MT_SET,
	.mark = 0,
	.con = NULL,
	.do_op = dpre_mtotal_set,
	.do_stop_op = NULL,
	.name = "set",
	},
	[EDI_PRE_MT_WAIT_INT] = {
	.id = EDI_PRE_MT_WAIT_INT,
	.mark = 0,
	.con = NULL,
	.do_op = dpre_mtotal_wait_int,
	.do_stop_op = NULL,
	.name = "wait_int",
	},
	[EDI_PRE_MT_TIME_OUT] = {
	.id = EDI_PRE_MT_TIME_OUT,
	.mark = 0,
	.con = NULL,
	.do_op = dpre_mtotal_timeout,
	.do_stop_op = NULL,
	.name = "timeout",
	},
};

/****************************
 *
 * mode for p
 *
 ****************************/
enum EDI_PRE_MP {
	EDI_PRE_MP_CHECK = K_DO_TABLE_ID_START,
	EDI_PRE_MP_SET,
	EDI_PRE_MP_WAIT_INT,
	EDI_PRE_MP_TIME_OUT,
	EDI_PRE_MP_CHECK2,
	EDI_PRE_MP_SET2,
	EDI_PRE_MP_WAIT_INT2,
	EDI_PRE_MP_TIME_OUT2,
};

unsigned int dpre_mp_check(void *data)
{
	struct di_hpre_s  *pre = get_hw_pre();
	unsigned int ret = K_DO_R_JUMP(K_DO_TABLE_ID_STOP);//K_DO_R_NOT_FINISH;
	struct di_ch_s *pch;

	pch = get_chdata(pre->curr_ch);

	if ((get_datal()->dct_op && get_datal()->dct_op->is_en(pch)) ||
	    pre_dbg_is_run()) {

		dim_print("%s:\n", __func__);
		if (!dim_pre_de_buf_config(pre->curr_ch)) {
			/*pre->flg_wait_int = false;*/
			/*pre_p_asi_set_next(pre->curr_ch);*/
			ret = K_DO_R_FINISH;
		} else {
			/*pre->flg_wait_int = false;*/
			ret = K_DO_R_JUMP(K_DO_TABLE_ID_STOP);
		}
		dim_dbg_pre_cnt(pre->curr_ch, "x");
	}

	return ret;
}

unsigned int dpre_mp_check2(void *data)
{
	struct di_hpre_s  *pre = get_hw_pre();
	unsigned int ret = K_DO_R_NOT_FINISH;

	if (!dim_pre_de_buf_config(pre->curr_ch)) {
		/*pre->flg_wait_int = false;*/
		ret = K_DO_R_FINISH;
	} else {
	}

	return ret;
}

unsigned int dpre_mp_wait_int(void *data)
{
	enum EDI_WAIT_INT wret;
	unsigned int ret = K_DO_R_NOT_FINISH;

	wret = di_pre_wait_int(NULL);
	switch (wret) {
	case EDI_WAIT_INT_NEED_WAIT:
		ret = K_DO_R_NOT_FINISH;
		break;
	case EDI_WAIT_INT_HAVE_INT:
		ret = K_DO_R_JUMP(EDI_PRE_MP_CHECK2);
		break;
	case EDI_WAIT_INT_TIME_OUT:
		ret = K_DO_R_FINISH;
		break;
	}
	return ret;
}

unsigned int dpre_mp_wait_int2(void *data)
{
	enum EDI_WAIT_INT wret;
	unsigned int ret = K_DO_R_NOT_FINISH;

	wret = di_pre_wait_int(NULL);
	switch (wret) {
	case EDI_WAIT_INT_NEED_WAIT:
		ret = K_DO_R_NOT_FINISH;
		break;
	case EDI_WAIT_INT_HAVE_INT:
		ret = K_DO_R_JUMP(K_DO_TABLE_ID_STOP);
		break;
	case EDI_WAIT_INT_TIME_OUT:
		ret = K_DO_R_FINISH;
		break;
	}
	return ret;
}

unsigned int dpre_mp_timeout(void *data)
{
	dpre_mtotal_timeout_contr();

	return K_DO_R_FINISH;
}

unsigned int dpre_mp_timeout2(void *data)
{
	dpre_mtotal_timeout_contr();

	return K_DO_R_JUMP(K_DO_TABLE_ID_STOP);
}

const struct do_table_ops_s pre_mode_proc[] = {
	/*fix*/
	[K_DO_TABLE_ID_PAUSE] = {
	.id = K_DO_TABLE_ID_PAUSE,
	.mark = 0,
	.con = NULL,
	.do_op = NULL,
	.do_stop_op = NULL,
	.name = "pause",
	},
	[K_DO_TABLE_ID_STOP] = {
	.id = K_DO_TABLE_ID_STOP,
	.mark = 0,			/*stop / pause*/
	.con = NULL,
	.do_op = NULL,
	.do_stop_op = NULL,
	.name = "stop",
	},
	/******************/
	[K_DO_TABLE_ID_START] = {	/*EDI_PRE_MP_CHECK*/
	.id = K_DO_TABLE_ID_START,
	.mark = 0,			/*stop / pause*/
	.con = NULL,
	.do_op = dpre_mp_check,
	.do_stop_op = NULL,
	.name = "start-check_p",
	},
	[EDI_PRE_MP_SET] = {
	.id = EDI_PRE_MP_SET,
	.mark = 0,			/*stop / pause*/
	.con = NULL,			/*condition*/
	.do_op = dpre_mtotal_set,
	.do_stop_op = NULL,
	.name = "pset",
	},
	[EDI_PRE_MP_WAIT_INT] = {
	.id = EDI_PRE_MP_WAIT_INT,
	.mark = 0,			/*stop / pause*/
	.con = NULL,			/*condition*/
	.do_op = dpre_mp_wait_int,
	.do_stop_op = NULL,
	.name = "pwait_int",
	},
	[EDI_PRE_MP_TIME_OUT] = {
	.id = EDI_PRE_MP_TIME_OUT,
	.mark = 0,			/*stop / pause*/
	.con = NULL,			/*condition*/
	.do_op = dpre_mp_timeout,
	.do_stop_op = NULL,
	.name = "ptimeout",
	},
	/******/
	[EDI_PRE_MP_CHECK2] = {		/*eDI_PRE_MP_CHECK2*/
	.id = EDI_PRE_MP_CHECK2,
	.mark = 0,			/*stop / pause*/
	.con = NULL,			/*condition*/
	.do_op = dpre_mp_check2,
	.do_stop_op = NULL,
	.name = "start-check_p2",
	},
	[EDI_PRE_MP_SET2] = {
	.id = EDI_PRE_MP_SET2,
	.mark = 0,			/*stop / pause*/
	.con = NULL,			/*condition*/
	.do_op = dpre_mtotal_set,
	.do_stop_op = NULL,
	.name = "psetp2",
	},
	[EDI_PRE_MP_WAIT_INT2] = {
	.id = EDI_PRE_MP_WAIT_INT2,
	.mark = 0,			/*stop / pause*/
	.con = NULL,			/*condition*/
	.do_op = dpre_mp_wait_int2,
	.do_stop_op = NULL,
	.name = "pwait_int2",
	},
	[EDI_PRE_MP_TIME_OUT2] = {
	.id = EDI_PRE_MP_TIME_OUT2,
	.mark = 0,			/*stop / pause*/
	.con = NULL,			/*condition*/
	.do_op = dpre_mp_timeout2,
	.do_stop_op = NULL,
	.name = "ptimeout2",
	},

};

void pre_mode_setting(void)
{
	struct di_hpre_s  *pre = get_hw_pre();

	if (pre->pre_st != EDI_PRE_ST4_DO_TABLE)
		return;

	do_table_working(&pre->sdt_mode);
}

/*--------------------------*/
enum EDI_WORK_MODE pre_cfg_count_mode(unsigned int ch, struct vframe_s *vframe)
{
	enum EDI_WORK_MODE pmode;

	if (is_bypass2(vframe, ch)) {
		pmode = EDI_WORK_MODE_BYPASS_ALL;
		return pmode;
	}

	if (COM_ME(vframe->type, VIDTYPE_INTERLACE)) {
		/*interlace:*/
		pmode = EDI_WORK_MODE_I;
		return pmode;
	}

	if (dimp_get(edi_mp_prog_proc_config) & 0x10)
		pmode = EDI_WORK_MODE_P_AS_P;
	else if (is_from_vdin(vframe))
		pmode = EDI_WORK_MODE_P_USE_IBUF;
	else
		pmode = EDI_WORK_MODE_P_AS_I;

	return pmode;
}

unsigned int dpre_check_mode(unsigned int ch)
{
	struct vframe_s *vframe;
	unsigned int mode;
	struct di_ch_s *pch;

	pch = get_chdata(ch);
	if ((get_datal()->dct_op && get_datal()->dct_op->is_en(pch)) ||
	    pre_dbg_is_run())
		vframe = nins_peekvfm_pre(pch);//pw_vf_peek(ch);
	else
		vframe = NULL;

	if (!vframe)
		return EDI_WORK_MODE_NONE;
	if (pch->itf.flg_s4dw)
		return EDI_WORK_MODE_S4DW;

	mode = pre_cfg_count_mode(ch, vframe);/*EDI_WORK_MODE_ALL;*/

	return mode;
}

/*--------------------------*/
bool dpre_step4_idle(void)
{
	struct di_hpre_s  *pre = get_hw_pre();
	bool reflesh = false;
	unsigned int ch;

	ch = pre->curr_ch;
	if (!pw_try_sw_ch_next_pre(ch))
		return false;

	if (pre->idle_cnt >= DI_CHANNEL_NUB) {
		pre->idle_cnt = 0;
		return false;
	}
	pre->pres = get_pre_stru(pre->curr_ch);
	pre->psts = get_post_stru(pre->curr_ch);

	/*state*/
	pre->pre_st++;/*tmp*/
	reflesh = true;

	return reflesh;
}

bool dpre_step4_check(void)
{
	struct di_hpre_s  *pre = get_hw_pre();
	bool reflesh = false;
	unsigned int mode;

	/*get vframe and select mode
	 * now: fix use total table
	 */

	mode = dpre_check_mode(pre->curr_ch);

	if (mode == EDI_WORK_MODE_NONE) {
		pre->pre_st--;
		pre->idle_cnt++;
		return true;
	}
	pre->idle_cnt = 0;
	if (mode == EDI_WORK_MODE_S4DW) {
		do_table_init(&pre->sdt_mode,
			      &s4dw_hw_process[0],
			      ARRAY_SIZE(s4dw_hw_process));
		pre->sdt_mode.name = "s4dw";
	} else if (mode == EDI_WORK_MODE_P_AS_I) {
		do_table_init(&pre->sdt_mode,
			      &pr_mode_total[0],
			      ARRAY_SIZE(pr_mode_total));
		pre->sdt_mode.name = "mtotal";

	} else {
		do_table_init(&pre->sdt_mode,
			      &pr_mode_total[0],
			      ARRAY_SIZE(pr_mode_total));
		pre->sdt_mode.name = "mtotal";
	}
	do_talbe_cmd(&pre->sdt_mode, EDO_TABLE_CMD_START);

	/*state*/
	pre->pre_st++;
	reflesh = true;

	return reflesh;
}

bool dpre_step4_do_table(void)
{
	struct di_hpre_s  *pre = get_hw_pre();
	bool reflesh = false;

	if (do_table_is_crr(&pre->sdt_mode, K_DO_TABLE_ID_STOP)) {
		pre->pre_st = EDI_PRE_ST4_IDLE;
		reflesh = true;
	}
	return reflesh;
}

bool dpre_after_do_table(void)
{
	struct di_hpre_s  *pre = get_hw_pre();
	enum EDI_PRE_ST4 pre_st;
	bool ret = false;

	if (do_table_is_crr(&pre->sdt_mode, K_DO_TABLE_ID_STOP)) {
		pre->pre_st = EDI_PRE_ST4_IDLE;
		ret = true;
	}
	pre_st = pre->pre_st;
	pre->self_trig_mask = 0;
	if (pre_run_flag == DI_RUN_FLAG_RUN)
		bclr(&pre->self_trig_mask, 0);
	else
		bset(&pre->self_trig_mask, 0);

	if (pre_st == EDI_PRE_ST4_DO_TABLE)
		bset(&pre->self_trig_mask, 1);
	else
		bclr(&pre->self_trig_mask, 1);
	return ret;
}

const struct di_func_tab_s di_pre_func_tab4[] = {
	{EDI_PRE_ST4_EXIT, NULL},
	{EDI_PRE_ST4_IDLE, dpre_step4_idle},
	{EDI_PRE_ST4_CHECK, dpre_step4_check},
	{EDI_PRE_ST4_DO_TABLE, dpre_step4_do_table},
};

const char * const dpre_state_name4[] = {
	"EXIT",
	"IDLE",	/*switch to next channel?*/
	"CHECK",
	"DO_TABLE",
};

const char *dpre_state4_name_get(enum EDI_PRE_ST4 state)
{
	if (state > EDI_PRE_ST4_DO_TABLE)
		return "nothing";

	return dpre_state_name4[state];
}

bool dpre_process_step4(void)
{
	struct di_hpre_s  *pre = get_hw_pre();
	enum EDI_PRE_ST4 pre_st = pre->pre_st;
//ary 2020-12-09	ulong flags = 0;

	if (pre_st > EDI_PRE_ST4_EXIT) {
//ary 2020-12-09		spin_lock_irqsave(&plist_lock, flags);
		dim_recycle_post_back(pre->curr_ch);
		dpre_recyc(pre->curr_ch);
		dpre_vdoing(pre->curr_ch);
//ary 2020-12-09		spin_unlock_irqrestore(&plist_lock, flags);
	}
	if (pre_st <= EDI_PRE_ST4_DO_TABLE	&&
	    di_pre_func_tab4[pre_st].func) {
		return di_pre_func_tab4[pre_st].func();
	}

	return false;
}
