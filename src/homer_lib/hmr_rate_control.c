/*****************************************************************************
 * hmr_rate_control.c : homerHEVC encoding library
/*****************************************************************************
 * Copyright (C) 2014 homerHEVC project
 *
 * Juan Casal <jcasal@homerhevc.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#include "hmr_private.h"
#include "hmr_common.h"
#include <math.h>




void hmr_rc_init(hvenc_engine_t* enc_engine)
{
	enc_engine->rc.vbv_size = enc_engine->vbv_size*1000;
	enc_engine->rc.vbv_fullness = enc_engine->vbv_init*1000;
	enc_engine->rc.average_pict_size = enc_engine->bitrate*1000/enc_engine->frame_rate;
	enc_engine->rc.average_bits_per_ctu = enc_engine->rc.average_pict_size/enc_engine->pict_total_ctu;
	enc_engine->hvenc->rc = enc_engine->rc;
}

void hmr_rc_init_seq(hvenc_engine_t* enc_engine)
{
}


void hmr_rc_gop(hvenc_engine_t* enc_engine)
{
}

//for scene changes in P frames
void hmr_rc_change_pic_mode(henc_thread_t* et, slice_t *currslice)
{
	hvenc_engine_t* enc_engine = et->enc_engine;
	int clipped_intra_period = (enc_engine->intra_period==0)?20:enc_engine->intra_period;
	if(enc_engine->is_scene_change)
	{
		int ithreads;
		double pic_size_new;
		if(et->enc_engine->gop_reinit_on_scene_change && enc_engine->rc.vbv_fullness<.5*enc_engine->rc.vbv_size)
		{
			pic_size_new = 1.*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);
//			pic_size_new = (1.5-((double)enc_engine->avg_dist/15000.))*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);
//			pic_size_new = 1.*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);	
		}
		else
		{
			pic_size_new = .75*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);	
		}
		enc_engine->rc.target_pict_size = pic_size_new;//.75*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);	

		enc_engine->rc.target_bits_per_ctu = enc_engine->rc.target_pict_size/enc_engine->pict_total_ctu;

		for(ithreads=0;ithreads<enc_engine->wfpp_num_threads;ithreads++)
		{
			henc_thread_t* henc_th = enc_engine->thread[ithreads];
		
			henc_th->target_pict_size = (uint32_t)enc_engine->rc.target_pict_size;
		}
	}
	enc_engine->hvenc->rc = enc_engine->rc;
}

void hmr_rc_init_pic(hvenc_engine_t* enc_engine, slice_t *currslice)
{
	int ithreads;
	int clipped_intra_period = enc_engine->intra_period==0?20:enc_engine->intra_period;
	double intra_avg_size = 2.25*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);

	enc_engine->rc = enc_engine->hvenc->rc;
	switch(currslice->slice_type)
	{
		case  I_SLICE:
		{
			if(currslice->slice_type == I_SLICE && enc_engine->intra_period!=1)
				enc_engine->pict_qp = hmr_rc_compensate_qp_for_intra(enc_engine->avg_dist, enc_engine->pict_qp);		
			currslice->qp = enc_engine->pict_qp;
//			enc_engine->rc.target_pict_size = (2.25-((double)enc_engine->avg_dist/15000.))*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);
			enc_engine->rc.target_pict_size = intra_avg_size;///*(2.25-((double)enc_engine->avg_dist/15000.))**/2.25*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);
//			enc_engine->rc.target_pict_size = /*(2.25-((double)enc_engine->avg_dist/15000.))**/2.*enc_engine->rc.average_pict_size*sqrt((double)clipped_intra_period);
			break;
		}
		case  P_SLICE:
		{
			currslice->qp = enc_engine->pict_qp;
			enc_engine->rc.target_pict_size = (enc_engine->rc.average_pict_size*clipped_intra_period-intra_avg_size)/(clipped_intra_period-1);//.5*enc_engine->rc.average_pict_size;
			break;	
		}
		case  B_SLICE:
		{
			enc_engine->rc.target_pict_size = enc_engine->rc.average_pict_size/2;
			break;	
		}
	}

#ifdef COMPUTE_AS_HM
	currslice->qp = enc_engine->pict_qp-(enc_engine->num_encoded_frames%4);
	if(currslice->qp<1)
	{
		currslice->qp=1;
	}
#endif

	enc_engine->rc.target_bits_per_ctu = enc_engine->rc.target_pict_size/enc_engine->pict_total_ctu;

	for(ithreads=0;ithreads<enc_engine->wfpp_num_threads;ithreads++)
	{
		henc_thread_t* henc_th = enc_engine->thread[ithreads];
		
		henc_th->target_pict_size = (uint32_t)enc_engine->rc.target_pict_size;
		henc_th->num_encoded_ctus = 0;
		henc_th->num_bits = 0;
		henc_th->acc_qp = 0;
	}
	enc_engine->hvenc->rc = enc_engine->rc;
}


double hmr_rc_compensate_qp_for_intra(double avg_dist, double qp)
{
	return qp/(1.5-(avg_dist/15000.));
}

double hmr_rc_compensate_qp_from_intra(double avg_dist, double qp)
{
	return qp*(1.5-(avg_dist/15000.));
}


void hmr_rc_end_pic(hvenc_engine_t* enc_engine, slice_t *currslice)
{
	double consumed_bitrate = 0.0;
	int consumed_ctus = 0;
	int avg_qp = 0;
	int ithreads, imods;
	int avg_rate_period = enc_engine->intra_period==0?100:enc_engine->intra_period;

	enc_engine->rc.vbv_fullness += enc_engine->rc.average_pict_size;

	consumed_ctus += enc_engine->pict_total_ctu;

	consumed_bitrate = hmr_bitstream_bitcount(&enc_engine->slice_bs);

	if(currslice->slice_type == I_SLICE && enc_engine->intra_period!=1)
	{
		if(1)//enc_engine->rc.vbv_fullness<.5*enc_engine->rc.vbv_size)
		{
			enc_engine->rc.acc_rate += consumed_bitrate/2;// - enc_engine->rc.average_pict_size;
			consumed_bitrate /=2; //consumed_bitrate = enc_engine->rc.average_pict_size;
		}
		else
		{
			double bits_to_apply = consumed_bitrate/2;//enc_engine->rc.average_pict_size + (consumed_bitrate-enc_engine->rc.average_pict_size)*(enc_engine->rc.vbv_fullness/enc_engine->rc.vbv_size-.5);
			enc_engine->rc.acc_rate += consumed_bitrate-bits_to_apply;// - 2*enc_engine->rc.average_pict_size;
			consumed_bitrate = bits_to_apply;///=2;// 2*enc_engine->rc.average_pict_size;			
		}
		enc_engine->rc.acc_avg = enc_engine->rc.acc_rate/avg_rate_period;
		enc_engine->rc.vbv_fullness -= consumed_bitrate+enc_engine->rc.acc_avg;	
		enc_engine->rc.acc_rate -= enc_engine->rc.acc_avg;
	}
	else if((enc_engine->is_scene_change) && enc_engine->intra_period!=1)// && )
	{
		if(enc_engine->rc.vbv_fullness<.5*enc_engine->rc.vbv_size)// && enc_engine->gop_reinit_on_scene_change)
		{
			enc_engine->rc.acc_rate += consumed_bitrate - enc_engine->rc.average_pict_size;
			consumed_bitrate = enc_engine->rc.average_pict_size;
		}
		else
		{
			enc_engine->rc.acc_rate += consumed_bitrate/2;// - 2*enc_engine->rc.average_pict_size;
			consumed_bitrate /=2;// 2*enc_engine->rc.average_pict_size;			
		}
		enc_engine->rc.acc_avg = enc_engine->rc.acc_rate/avg_rate_period;
		enc_engine->rc.vbv_fullness -= consumed_bitrate+enc_engine->rc.acc_avg;	
		enc_engine->rc.acc_rate -= enc_engine->rc.acc_avg;
	}
/*	else if(consumed_bitrate>3.0*enc_engine->rc.average_pict_size && enc_engine->is_scene_change && enc_engine->rc.vbv_fullness<.75*enc_engine->rc.vbv_size)
	{
		enc_engine->rc.acc_rate += consumed_bitrate - 3.0*enc_engine->rc.average_pict_size;
		enc_engine->rc.acc_avg = enc_engine->rc.acc_rate/enc_engine->intra_period;
		consumed_bitrate = 3.0*enc_engine->rc.average_pict_size;
		enc_engine->rc.vbv_fullness -= consumed_bitrate;	
	}
*/	else
	{
		//vbr
		if(enc_engine->bitrate_mode == BR_VBR)
		{
			if(currslice->slice_type != I_SLICE)
			{
				if(consumed_bitrate<.45*enc_engine->rc.target_pict_size && enc_engine->rc.vbv_fullness<.75*enc_engine->rc.vbv_size)
				{
					enc_engine->rc.acc_rate += (.005*enc_engine->rc.vbv_size);//2*enc_engine->rc.average_pict_size;
					consumed_bitrate -= (.005*enc_engine->rc.vbv_size);//2*enc_engine->rc.average_pict_size;

		//			enc_engine->rc.acc_rate += consumed_bitrate;
		//			consumed_bitrate = 0;//;
					enc_engine->rc.acc_avg = enc_engine->rc.acc_rate/avg_rate_period;
				}
				else if(consumed_bitrate>1.55*enc_engine->rc.target_pict_size && enc_engine->rc.vbv_fullness>.1*enc_engine->rc.vbv_size)// && enc_engine->rc.vbv_fullness<.75*enc_engine->rc.vbv_size)// && enc_engine->rc.acc_rate>0)
				{
					enc_engine->rc.acc_rate -= (.005*enc_engine->rc.vbv_size);//2*enc_engine->rc.average_pict_size;
					consumed_bitrate += (.005*enc_engine->rc.vbv_size);//2*enc_engine->rc.average_pict_size;

		//			enc_engine->rc.acc_rate += consumed_bitrate;
		//			consumed_bitrate = 0;//;
					enc_engine->rc.acc_avg = enc_engine->rc.acc_rate/avg_rate_period;
				}
			}
		}

		enc_engine->rc.vbv_fullness -= consumed_bitrate;
		enc_engine->rc.vbv_fullness -= enc_engine->rc.acc_avg;
		enc_engine->rc.acc_rate -= enc_engine->rc.acc_avg;
	}

	if(enc_engine->rc.vbv_fullness>enc_engine->rc.vbv_size)
	{
#ifdef DBG_TRACE
		printf("HomerHEVC - vbv_overflow: efective bitrate is lower than expected\r\n");
#endif // DBG_TRACE

		enc_engine->rc.vbv_fullness=enc_engine->rc.vbv_size;
	}

	if(enc_engine->rc.vbv_fullness<0)
	{
#ifdef DBG_TRACE
		printf("HomerHEVC - vbv_underflow: efective bitrate is higher than expected\r\n");
#endif
		enc_engine->rc.vbv_fullness=0;
	}
	enc_engine->hvenc->rc = enc_engine->rc;
}



int hmr_rc_calc_cu_qp(henc_thread_t* curr_thread, ctu_info_t *ctu, cu_partition_info_t *curr_cu_info, slice_t *currslice)
{
	hvenc_engine_t* enc_engine = curr_thread->enc_engine;
	int ithreads;
	double qp;
	double pic_corrector = 0.0;
	double vbv_corrector;
	double consumed_bitrate = 0.0, entropy;
	double min_vbv_size;
	int consumed_ctus = 0;
	for(ithreads=0;ithreads<enc_engine->wfpp_num_threads;ithreads++)
	{
		henc_thread_t* henc_th = enc_engine->thread[ithreads];
		
		consumed_bitrate += henc_th->num_bits;
		consumed_ctus += henc_th->num_encoded_ctus;
	}

	entropy = 3;//sqrt(((double)enc_engine->avg_dist/3000.)*(curr_cu_info->variance))/40;//25.0;

	if(consumed_ctus>0)
	{
		if(consumed_bitrate>1.5*enc_engine->rc.target_bits_per_ctu)//*consumed_ctus && currslice->slice_type != I_SLICE)
			pic_corrector = .0125*(consumed_bitrate/(enc_engine->rc.target_bits_per_ctu*consumed_ctus));

		if(currslice->slice_type != P_SLICE)
			pic_corrector *= 2.;//0125*(consumed_bitrate/(enc_engine->rc.target_bits_per_ctu*consumed_ctus));
	}
	else
		pic_corrector = 0;

	pic_corrector = 0;
	consumed_bitrate = 0;
	min_vbv_size = clip(enc_engine->rc.vbv_fullness,enc_engine->rc.vbv_fullness,enc_engine->rc.vbv_size*.95);


	if(consumed_bitrate>enc_engine->rc.target_bits_per_ctu*consumed_ctus)
		vbv_corrector = 1.0-clip((min_vbv_size-consumed_bitrate+enc_engine->rc.target_bits_per_ctu*consumed_ctus)/enc_engine->rc.vbv_size, 0.0, 1.0);
	else
		vbv_corrector = 1.0-clip((min_vbv_size)/enc_engine->rc.vbv_size, 0.0, 1.0);
	qp = ((pic_corrector+vbv_corrector)/1.)*(MAX_QP)+/*(pic_corrector-1)+*/(entropy-3.);

	//variable rate
	if(enc_engine->bitrate_mode == BR_VBR)
	{
		if(qp<enc_engine->qp_min)
			qp=enc_engine->qp_min;
	}

	if(curr_thread->enc_engine->intra_period>1)
	{
		if(currslice->slice_type == I_SLICE || (enc_engine->is_scene_change && enc_engine->gop_reinit_on_scene_change))
		{
			qp/=clip(1.5-((double)enc_engine->avg_dist/7500.),1.15,1.35);
//			qp/=1.4-((double)enc_engine->avg_dist/50000.);
		}
		else if(enc_engine->is_scene_change)
			qp/=1.1;
	}

	if((enc_engine->is_scene_change) && qp<=5)
	{
		qp=5;
	}

	if(enc_engine->num_encoded_frames==0)
	{
		qp+=4;
	}
	else if(currslice->slice_type == I_SLICE && consumed_bitrate > 1.*(enc_engine->rc.target_bits_per_ctu*consumed_ctus) && enc_engine->rc.vbv_fullness<.5*enc_engine->rc.vbv_size)//control scene changes in I frames
	{
		qp+=2;
	}

	return (int)clip(qp+.5,/*MIN_QP*/1.0,MAX_QP);
}


int hmr_rc_get_cu_qp(henc_thread_t* et, ctu_info_t *ctu, cu_partition_info_t *curr_cu_info, slice_t *currslice)
{
	int qp;
#ifdef COMPUTE_AS_HM
	double debug_qp = currslice->qp+ctu->ctu_number%4;//28+ctu->ctu_number%4;
	if(et->enc_engine->bitrate_mode == BR_FIXED_QP)
	{
		qp = et->enc_engine->current_pict.slice.qp;
	}
	else//cbr, vbr
	{
		if(curr_cu_info->depth <= et->enc_engine->qp_depth)
			qp = (int)debug_qp;//hmr_rc_calc_cu_qp(et);
		else
			qp = curr_cu_info->parent->qp;
	}
#else
	if(et->enc_engine->bitrate_mode == BR_FIXED_QP)
	{
		qp = et->enc_engine->current_pict.slice.qp;
	}
	else//cbr, vbr
	{
//		if(et->enc_engine->qp_depth==0 )
//			qp = hmr_rc_calc_cu_qp(et);
		if(curr_cu_info->depth <= et->enc_engine->qp_depth)
		{
			qp = hmr_rc_calc_cu_qp(et, ctu, curr_cu_info, currslice);	
		}
		else
			qp = curr_cu_info->parent->qp;
	}
#endif

	return qp;
}
