/*
 * fx_filters.c -  various 4th-order filters for F373 Mod
 * 03-29-20 E. Brombaugh
 */
 
#include "fx_filters.h"
#include "ifilter_mg4_v1.h"

typedef struct 
{
	uint8_t type;
	int16_t fc;
	ifmg4_state fs[2];
} fx_filter_blk;

const char *filter_param_names[] =
{
	"Cutoff",
	"Resnnc",
	"",
};

/*
 * Common filter init
 */
void * fx_filters_Init(uint32_t *mem, uint8_t type)
{
	/* set up instance in mem area provided */
	fx_filter_blk *blk = 	(fx_filter_blk *)mem;
	
	/* set channel and type */
	blk->type = type;
		
	/* initialize filter blocks */
	init_ifilter_mg4(&blk->fs[0]);
	init_ifilter_mg4(&blk->fs[1]);
	
	/* return pointer */
	return (void *)blk;
}

/*
 * low-pass init
 */
void * fx_lpf_Init(uint32_t *mem)
{
	return fx_filters_Init(mem, 0);
}

/*
 * high-pass init
 */
void * fx_hpf_Init(uint32_t *mem)
{
	return fx_filters_Init(mem, 2);
}

/*
 * band-pass init
 */
void * fx_bpf_Init(uint32_t *mem)
{
	return fx_filters_Init(mem, 3);
}

/*
 * VCA audio process
 */
void IRAM_ATTR fx_filters_Proc(void *vblk, int16_t *dst, int16_t *src, uint16_t sz)
{
	fx_filter_blk *blk = vblk;
	int32_t fc, res;
	
	/* update filter params for this pass */
	fc = adc_param[1]<<3;
	fc = (((fc*fc)>>15)*fc)>>15;
	blk->fc = fc;
	res = adc_param[2]<<3;
	set_ifilter_mg4(&blk->fs[0], fc, res, blk->type);
	dupe_ifilter_mg4(&blk->fs[0], &blk->fs[1]);
	
	/* loop over the buffer */
	while(sz--)
	{
		/* run the filters */
		*dst++ = ifilter_mg4(&blk->fs[0], *src++);
		*dst++ = ifilter_mg4(&blk->fs[1], *src++);
	}
}

/*
 * Render parameter for clean delay - either delay in ms or feedback %
 */
void fx_filters_Render_Parm(void *vblk, uint8_t idx, GFX_RECT *rect)
{
	fx_filter_blk *blk = vblk;
	char txtbuf[32];
	float32_t cutoff;
	
	switch(idx)
	{
		case 1:	// cutoff
			cutoff = ((float32_t)SAMPLE_RATE / 2) * ((float32_t)blk->fc/32768.0F) / 1000.0F;
			sprintf(txtbuf, "%4.2f kHz ", cutoff);
			break;
		
		case 2:	// Feedback
			sprintf(txtbuf, "%2d%% ", adc_param[idx]/41);
			break;
		
		default:
			return;
	}
	gfx_drawstrrect(rect, txtbuf);
}

/*
 * low-pass filter struct
 */
fx_struct fx_lpf_struct =
{
	"LPF",
	2,
	filter_param_names,
	fx_lpf_Init,
	fx_bypass_Cleanup,
	fx_filters_Proc,
	fx_filters_Render_Parm,
};

/*
 * high-pass filter struct
 */
fx_struct fx_hpf_struct =
{
	"HPF",
	2,
	filter_param_names,
	fx_hpf_Init,
	fx_bypass_Cleanup,
	fx_filters_Proc,
	fx_filters_Render_Parm,
};

/*
 * low-pass filter struct
 */
fx_struct fx_bpf_struct =
{
	"BPF",
	2,
	filter_param_names,
	fx_bpf_Init,
	fx_bypass_Cleanup,
	fx_filters_Proc,
	fx_filters_Render_Parm,
};

