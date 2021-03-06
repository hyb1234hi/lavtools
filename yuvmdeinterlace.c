/*
 *  yuvdeinterlace.c
 *  deinterlace  2004 Mark Heath <mjpeg at silicontrip.org>
 *  converts interlaced source material into progressive by doubling the frame rate and adaptively interpolating required pixels.

**<h3>Double frame rate de-interlacer</h3>
**
**<p>A non destructive deinterlacer.  Converts to a double frame
**rate, progressive yuv stream for further processing
**by temporal based non interlace aware filters.
**Then re-interlaced or frame rate converted before encoding.</P>
**
**<p>This program uses an experimental detection algorithm to detect interlaced
**pixels and then uses cubic interpolation to replace pixels that are suffering
**comb effect.  Image data is not lost as the frame rate is doubled.  This
**adaptive algorithm is quite effective, however not ideal.  Noisy artefacts
**may be apparent in (some) most material.
**</P>
**<p>
**I have been looking into implimenting a 4 point DFT on the data and
**looking at the amplitude of the high (/2) frequency component.  This has lead
**to better results but appears to still have issues at the boundary of
**interlace and non interlace material.
**</p>
**<p>
**The above code could be extended to an arbitary number of pixels, comparing
**the /2 (interlace) frequency with the /4 frequency.</p>
**<p>
**This release of the code I have played with comparing both the high frequency
**and the next frequency component.  The high frequency shows areas of interlace
**however also detects edges.  The second frequency component detects edges.
**By balancing these two results, by trial and error, I beleive to have come up
**with a better detection algorithm.   However  non interpolated interlace artefacts
**are still apparent.  This is the best results so far.
**</p>
**<p>
**Just when I thought I tuned the DFT algorithm to the best parameters.  I came up
**with another method that simply looked for a &quot;greater than, smaller than, greater
**than&quot; pattern and as a result are now detecting interlace much more accurately than
**before.  I also added an additional test on the AC value to eliminate non noticable
**interlace (or noise).  This version is now available.
**</p>
**<p>
**I am extremely happy with the results.  Although not perfect, there are false positives,
**and a few false negatives.  Of course I will be fine tuning this filter even more over
**time.  For now I would use it as part of a yuv filter chain for my production videos.
**</p>
**<p> I have implemented a YUV filter of yadif de-interlacer. I beleive it produces better
**results than this filter.  I am ceasing work on this filter.  Unless some novel de-interlace
**algorithm comes out.</p>
**
**<h4>TODO</h4>
**<p>
**Write a pixel merging algorithm for conversion of 25i material to 25p (rather than 50p).<br>
**Look at ways to perform anti-aliasing for fixing up nearest neighbour de-interlace filters. These sorts of filters
**should be shot, as it's quite easy to write a linear interpolator, not to difficult to write a cubic interpolator,
**and look I just wrote an adaptive type de-interlacer.  I can't believe I saw this kind of de-interlace filter used
**on an episode of Top Gear the other night.  Disgraceful!<br>
**Provide a screenshot...
**</p>
**
**<h4>Ideas</h4>
**
**<p> I have been experimenting with training a neural net to *learn*
**what the missing pixel may be from the surrounding pixels.  My
**first attempts have not worked, if it is trained from images with
**many uphill lines, then uphill interpolation looks fantastic but
**downhill is very jaggered.</p>
**
**<p> I am thinking about increasing the number of hidden layers in
**the neural net to see if it improves interpolation.  The code is
**in java, I'll just need some encouragement to look at it again.</p>
**
**<p>
**This part of the code would replace the cubic interpolator.
**Detection would still be separate algorithm, however comparing
**the interpolated pixel with the real pixel and replacing the real
**pixel with the interpolated if it is different by a threshold amount.
**May prove a better algorithm.
**</p>
**
**<UL>
**
**<li>29th April 2008 Tuned the DFT parameters to optimal. However artefacts are still apparent.</li>
**<li>13th April 2008 Implemented a 4 point DFT interlace detection algorithm.</li>
**<li>10th April 2008 Implemented a cubic interpolation algorithm. Do you know
**how hard it is to find programmatic information about this on the web?
**<li>6th April 2008 Optimised the code for better performance.</li>
**<li>October 2007 Changed the code to an adaptive deinterlace filter. With linear interpolation.  The re-interlace code is probably not needed as my yuvafps does a better job.</li>
**<li> 26th April 2005.  Fixed a bug which incorrectly detected
**the end of file, creating more frames in the output.</li>
**<li>9th April 2005 New features which now produces a full height
**frame, by line duplication.  Also contains code from yuvdeprogress,
**to re-interlace the stream.</li>
**
**</ul>
**
**<h4>Example</h4>
**<p> by default yuvdeinterlace will double the frame rate and convert fields
**into full height frames. No arguments needed.</P>

 *  based on code:
 *  Copyright (C) 2002 Alfonso Garcia-PatiÒo Barbolani <barbolani at jazzfree.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *

 gcc yuvdeinterlace.c -I/sw/include/mjpegtools -L/sw/lib -lmjpegutils -o yuvdeinterlace
 gcc yuvdeinterlace.c -I/usr/local/include/mjpegtools -L/usr/local/lib -lmjpegutils  -arch ppc -arch i386 -o yuvdeinterlace


 */


// minimum number of pixels needed to detect interlace.
#define PIXELS 4


#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <math.h>

#include <yuv4mpeg.h>
#include <mpegconsts.h>
#include "utilyuv.h"

#define YUVDE_VERSION "1.6.1"

typedef struct frame_dimensions {
	size_t plane_length_luma;
	size_t plane_length_chroma;
	size_t plane_width_luma;
	size_t plane_width_chroma;
	size_t plane_height_luma;
	size_t plane_height_chroma;

	size_t chroma_width_ratio;
	size_t chroma_height_ratio;

} frame_dimensions;


// OO style globals, with getters and setters
static int setting_mark=0;
static int setting_interpolate=0;
static int setting_fullframe=0;

void setInterpolate (int i) { setting_interpolate = i; }
int getInterpolate () { return setting_interpolate; }

void setMark(int i) { setting_mark = i; }
int getMark() { return setting_mark; }

void setFullframe(int i) { setting_fullframe = i; }
int getFullframe() { return setting_fullframe; }



static void print_usage()
{
	fprintf (stderr,
			 "usage: yuvdeinterlace [-v] [-It|b] [-f] [-m0-4] [-h]\n"
			 "yuvdeinterlace  deinterlaces source material by\n"
			 "doubling the frame rate and interpolating the interlaced scanlines.\n"
			 "\n"
			 "\t -i Interpolator mode. 0: cubic (default). 1: linear. 2: nearest neighbour\n"
			 "\t -m Operation mode: \n"
			 "\t\t 0: deinterlace to full height double framerate (default)\n"
			 "\t\t 1: Mark interlaced pixels (useful for debugging the detection algorithm)\n"
			 "\t\t 2: deinterlace to half height double framerate. (only good for spacial filters)\n"
			 "\t\t 3: deinterlace to full height same frame rate. (Merges both fields into one)\n"
			 "\t\t 4: Interlace to full height half frame rate. (re-interlace)\n"
			 "\t\t 5: deinterlace to full height same frame rate. (Drops one field)\n"
			 "\t -I[t|b|p] force interlace mode\n"
			 "\t -f deinterlace entire frame (no adaptive detection)\n"
			 "\t -v Verbosity degree : 0=quiet, 1=normal, 2=verbose/debug\n"
			 "\t -h print this help\n"
			 );
}


//Copy a uint8_t frame
// The frame dimensions version.

void chromacpyfd(uint8_t *m[3],uint8_t *n[3],frame_dimensions *fd)
{

	int fs,cfs;

	fs = fd->plane_length_luma;
	cfs = fd->plane_length_chroma;

	memcpy (m[0],n[0],fs);
	memcpy (m[1],n[1],cfs);
	memcpy (m[2],n[2],cfs);

}


int int_detect3 (int x, int y,uint8_t *m[3],frame_dimensions *fd)
{
	uint8_t luma[4];
	int i,t,w,h;

	w = fd->plane_width_luma;
	h = fd->plane_height_luma;


	// 3 pixel detection
	t=y-1;

	for(i=0; i<3;i++)
		if ((i+t<0)||(i+t>=h)) {
			luma[i]=128;
			//	mean += 128;
		} else {
			luma[i] = m[0][(i+t)*w+x];
			//	mean += m[0][i*w+x];
		}

	i=0;
	if (luma[0] < luma[1] && luma[2] < luma[1]) i=1;
	if (luma[0] > luma[1] && luma[2] > luma[1]) i=1;

	return i;

}

int int_detect2 (int x, int y,uint8_t *m[3],frame_dimensions *fd) {

	uint8_t luma[4];
	int i,w,h,t;
	int m1,m2,m3,m4;

	//int mean=0;

	// mjpeg_warn("int_detect");

	w = fd->plane_width_luma;
	h = fd->plane_height_luma;

	t = y -2 ;

	// Unroll this loop
	// read the pixels above and below the target pixel.
	for(i=0; i<4;i++)
		if ((i+t<0)||(i+t>=h)) {
			luma[i]=128;
		//	mean += 128;
		} else {
			luma[i] = m[0][(i+t)*w+x];
		//	mean += m[0][i*w+x];
		}

	m1 = luma[0]>luma[2]?luma[0]:luma[2];
	m2 = luma[0]>luma[2]?luma[2]:luma[0];
	m3 = luma[1]>luma[3]?luma[1]:luma[3];
	m4 = luma[1]>luma[3]?luma[3]:luma[1];



	// why is this +2 ?
	// mean = (mean+2) / 4;

	//	mean /= 4;

	i=0;
	// for something with a definable PIXELS this sure uses hard coded 4 values...
	//if (luma[0] < mean && luma[1] > mean && luma[2] < mean && luma[3] > mean) i=1;
	//if (luma[1] < mean && luma[0] > mean && luma[3] < mean && luma[2] > mean) i=1;

	if (m1<m4 || m2>m3) i=1;

	// I don't really want to print out every luma

	//fprintf (stderr,"%d: %d,%d,%d,%d\n",i,luma[0],luma[1],luma[2],luma[3]);

	return i;

}
int int_detect (int x, int y,uint8_t *m[3],frame_dimensions *fd) {

	uint8_t luma[PIXELS];
	int hp = PIXELS/2;
	int i,w,h;

	int br,bi,cr,ci;
	int b,c;

	// mjpeg_warn("int_detect");


	w = fd->plane_width_luma;
	h = fd->plane_height_luma;

	// Unroll this loop
	// read the pixels above and below the target pixel.
	for(i=y-hp; i<y+PIXELS-hp;i++)
		if ((i<0)||(i>=h))
			luma[i+hp-y]=128;
		else
			luma[i+hp-y] = m[0][i*w+x];

	// perform a 4 point DFT
	// taking only the components we need


	//ar = luma[0] + luma[1] + luma[2] + luma[3];
	//	ai = 0;
	br = luma[0] - luma[2];
	bi = luma[3] - luma[1];
	cr = luma[0] - luma[1] + luma[2] - luma[3];
	ci = 0;
	//	dr = br;
	//	di = -bi;

	// ar >>= 1;
	// ai >>= 1;
	br >>= 1;
	bi >>= 1;
	cr >>= 1;
	// ci >>= 1;
	//	dr >>= 1;
	//	di >>= 1;

	// we are not going to perform square root
	// a = ar * ar;
	b = br * br + bi * bi;
	c = cr * cr;
	//	d = sqrt ( dr * dr + di * di);

	//	printf ("%d %d\n",c,b);

	/*
	 if (c>127) {
	 printf ("Interlace luma values: %d,%d,%d,%d  DCT coefficients %d-%d\n", luma[0],luma[1],luma[2],luma[3],c,b);
	 }
	 */

	// if the power of the ac frequency is heigher than the half frequency


	// find simple alternating pattern and areas where the AC is strong.
	if (c > b && c > 12 && ( (luma[0] < luma[1] && luma[2] < luma[1] && luma[2] < luma[3] ) ||
							(luma[0] > luma[1] && luma[2] > luma[1] && luma[2] > luma[3] )) )  {

		//	printf ("%d %d\n",c,b);
		return 1;
	}

	return 0;


	// trial and error
	// c is the high frequency amount, so above a threshold it should trigger
	// however b is the "edge" amount which can cause large c values.
	//return (c>128 && c > (b<<2));

	return (c>12 && c > (b<<2));
}

static void mark_deint_pixels (int x, int y,uint8_t *m[3],uint8_t *n[3],frame_dimensions *fd)
{

	int w,h,cw,ch,cwr,chr;

	h = fd->plane_height_luma;
	w = fd->plane_width_luma;
	ch = fd->plane_height_chroma;
	cw = fd->plane_width_chroma;

	cwr = w / cw;
	chr = h / ch;

	m[0][x+y*w] = 128;
	m[1][x/cwr+y/chr*cw] = 128;
	m[2][x/cwr+y/chr*cw] = 192;

}

uint8_t nearest_interpolate (uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3) { return v1; }

uint8_t linear_interpolate (uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3) {


	int r = v2 - v1;

  	// Assuming halfway between the pixels.
	return (r >> 1) + v1;

}

// Perform a cubic interpolation

uint8_t cubic_interpolate (uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3) {

	int p = (v3 - v2) - (v0 - v1);
	int q = (v0 - v1) - p;
	int r = v2 - v0;

	// Assuming halfway between the pixels.

	int tot = (p >> 3) + (q >> 2) + (r >> 1) + v1;

	if (tot > 255) {
		// 	fprintf (stderr,"pixel overflow %d (%d,%d,%d,%d)\n",tot,v0,v1,v2,v3);
		tot = 255;
	}
	if (tot < 0) {
		// fprintf (stderr,"pixel overflow %d (%d,%d,%d,%d)\n",tot,v0,v1,v2,v3);
		tot = 0;
	}

	return tot;

}

// wrapper for the scaling algorithm
uint8_t scalar_interpolate (uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3) {

	switch (getInterpolate()) {
		case 0:
			return cubic_interpolate(v0,v1,v2,v3);
			break;
		case 1:
			return linear_interpolate(v0,v1,v2,v3);
			break;
		case 2:
			return nearest_interpolate(v0,v1,v2,v3);
			break;
		default:
			return cubic_interpolate(v0,v1,v2,v3);
			break;

	}
}

// interpolates a new pixel based on the 4 surrounding pixels from the same field
static void deint_pixels (int x, int y,uint8_t *m[3],uint8_t *n[3],frame_dimensions *fd)
{

	int w,h,cw;

	h = fd->plane_height_luma;
	w = fd->plane_width_luma;
//	ch = fd->plane_height_chroma;
	cw = fd->plane_width_chroma;

	if (y<=h) {

		int cwr,chr,xcwr,ychr,ychrn,ychrp,ychrcw;
		int ychrn2,ychrp2;
		int tluma, tchromu,tchromv;
		int chroma_posp,chroma_posn;
		int chroma_posp2,chroma_posn2;


		// most common values for cwr and chr are 1,2 or 4

		cwr = fd->chroma_width_ratio;
		chr = fd->chroma_height_ratio;

		// optimize for these common values

		if ( cwr == 1 ) {
			xcwr = x;
		} else if ( cwr == 2) {
			xcwr = x >> 1;
		} else if (cwr == 4) {
			xcwr = x >> 2;
		} else {
			xcwr = x / cwr;
		}

		if (chr == 1) {
			ychr = y;
			ychrn = y-1;
			ychrp = y+1;
			ychrn2 = y-3;
			ychrp2 = y+3;
		} else if (chr == 2) {
			ychr = ((y >> 2) << 1) + (y%2);
			// y%2 for interlace chroma
			ychrn = (((y-1) >> 2) << 1) + (1-(y%2));
			ychrp = (((y+1) >> 2) << 1) + (1-(y%2));
			ychrn2 = (((y-3) >> 2) << 1) + (1-(y%2));
			ychrp2 = (((y+3) >> 2) << 1) + (1-(y%2));

		} else if (chr == 4) {
		// I have no idea how a /4 interlace chroma would work.
			ychr = y >> 2;
			ychrn = (y-1) >> 2;
			ychrp = (y+1) >> 2;
			ychrn2 = (y-3) >> 2;
			ychrp2 = (y+3) >> 2;

		} else {
			ychr = y / chr;
			ychrn = (y-1) / chr;
			ychrp = (y+1) / chr;
			ychrn2 = (y-3) / chr;
			ychrp2 = (y+3) / chr;

		}

			//fprintf (stderr,"pix: (%d) %d %d %d %d\n",y,ychrn2,ychrn,ychrp,ychrp2);


		ychrcw = ychr * cw;

		chroma_posn = xcwr + ychrn * cw;
		chroma_posp = xcwr + ychrp * cw;
		chroma_posn2 = xcwr + ychrn2 * cw;
		chroma_posp2 = xcwr + ychrp2 * cw;

		// 4 point interpolate

		if (y==0) {
			tluma = scalar_interpolate(16,16,n[0][x+w],n[0][x+w*3]);
			tchromu = scalar_interpolate(128,128,n[1][xcwr + cw],n[1][xcwr + cw * 3]);
			tchromv = scalar_interpolate(128,128,n[2][xcwr + cw],n[1][xcwr + cw * 3]);
		} else if ((y == 1) || (y == 2)) {
			tluma = scalar_interpolate(16,n[0][x+w*(y-1)],n[0][x+w*(y+1)],n[0][x+w*(y+3)]);
			tchromu = scalar_interpolate(128,n[1][chroma_posn],n[1][chroma_posp],n[1][chroma_posp2]);
			tchromv = scalar_interpolate(128,n[2][chroma_posn],n[2][chroma_posp],n[2][chroma_posp2]);
		} else if (((y+3)==h) || ((y+2)==h)) {
			tluma = scalar_interpolate(n[0][x+w*(y-3)],n[0][x+w*(y-1)],n[0][x+w*(y+1)],16);
			tchromu = scalar_interpolate(n[1][chroma_posn2],n[1][chroma_posn],n[1][chroma_posp],128);
			tchromv = scalar_interpolate(n[2][chroma_posn2],n[2][chroma_posn],n[2][chroma_posp],128);
		} else if ((y+1) == h) {
			tluma = scalar_interpolate(n[0][x+w*(y-3)],n[0][x+w*(y-1)],16,16);
			tchromu = scalar_interpolate(n[1][chroma_posn2],n[1][chroma_posn],128,128);
			tchromv = scalar_interpolate(n[2][chroma_posn2],n[2][chroma_posn],128,128);
		} else {
			tluma = scalar_interpolate(n[0][x+w*(y-3)],n[0][x+w*(y-1)],n[0][x+w*(y+1)],n[0][x+w*(y+3)]);
			tchromu = scalar_interpolate(n[1][chroma_posn2],n[1][chroma_posn],n[1][chroma_posp],n[1][chroma_posp2]);
			tchromv = scalar_interpolate(n[2][chroma_posn2],n[2][chroma_posn],n[2][chroma_posp],n[2][chroma_posp2]);
		}


		if (chr == 1 && cwr == 1) {
			m[0][x+ychrcw] = tluma;
		} else {
			m[0][x+y*w] = tluma;
		}
		m[1][xcwr+ychrcw] = tchromu;
		m[2][xcwr+ychrcw] = tchromv;
	//	mjpeg_warn("deint pixels done");
	}

}

// Merges 4 interlace pixels into 1
static void merge_pixels (int x, int y,uint8_t *m[3],uint8_t *n[3],frame_dimensions *fd)
{

	int w,h,cw,cwr,chr,xcwr,ychr,ychrcw;
	int ychr1,ychr2,ychr3,ychr4;
	int tluma, tchromu,tchromv;
	int chroma_pos1,chroma_pos2, chroma_pos3,chroma_pos4;
	int y1,y2,y3,y4;

	h = fd->plane_height_luma;
	w = fd->plane_width_luma;
	//ch = fd->plane_height_chroma;
	cw = fd->plane_width_chroma;

	// even y
	y1 = y - 1;
	y2 = y ;
	y3 = y + 1;
	y4 = y + 2;

	// 	fprintf (stderr,"pix: %d %d %d %d\n",y1,y2,y3,y4);

	// most common values for cwr and chr are 1,2 or 4

	cwr = fd->chroma_width_ratio;
	chr = fd->chroma_height_ratio;

	// optimize for these common values

	if ( cwr == 1 ) {
		xcwr = x;
	} else if ( cwr == 2) {
		xcwr = x >> 1;
	} else if (cwr == 4) {
		xcwr = x >> 2;
	} else {
		xcwr = x / cwr;
	}

	if (chr == 1) {
		ychr = y;

		ychr1 = y1;
		ychr2 = y2;
		ychr3 = y3;
		ychr4 = y4;
	} else if (chr == 2) {
		ychr = y >> 1;
		ychr1 = y1 >> 1;
		ychr2 = y2 >> 1;
		ychr3 = y3 >> 1;
		ychr4 = y4 >> 1;

	} else if (chr == 4) {
		ychr = y >> 2;
		ychr1 = y1 >> 2;
		ychr2 = y2 >> 2;
		ychr3 = y3 >> 2;
		ychr4 = y4 >> 2;

	} else {
		ychr = y / chr;
		ychr1 = y1 / chr;
		ychr2 = y2 / chr;
		ychr3 = y3 / chr;
		ychr4 = y4 / chr;

	}

	ychrcw = ychr * cw;

	chroma_pos1 = xcwr + ychr1 * cw;
	chroma_pos2 = xcwr + ychr2 * cw;
	chroma_pos3 = xcwr + ychr3 * cw;
	chroma_pos4 = xcwr + ychr4 * cw;

	// 4 point interpolate

	if (y2==0) {
		tluma = scalar_interpolate(16,n[0][x],n[0][x+w],n[0][x+w*2]);
		tchromu = scalar_interpolate(128,n[1][xcwr],n[1][xcwr + cw],n[1][xcwr + cw * 2]);
		tchromv = scalar_interpolate(128,n[1][xcwr],n[2][xcwr + cw],n[1][xcwr + cw * 2]);
	} else if (y2 == h-1) {
		tluma = scalar_interpolate(n[0][x+w*y1],n[0][x+w*y2],16,16);
		tchromu = scalar_interpolate(n[1][chroma_pos1],n[1][chroma_pos2],128,128);
		tchromv = scalar_interpolate(n[2][chroma_pos1],n[2][chroma_pos2],128,128);
	} else if (y3 == h-1) {
		tluma = scalar_interpolate(n[0][x+w*y1],n[0][x+w*y2],n[0][x+w*y3],16);
		tchromu = scalar_interpolate(n[1][chroma_pos1],n[1][chroma_pos2],n[1][chroma_pos3],128);
		tchromv = scalar_interpolate(n[2][chroma_pos1],n[2][chroma_pos2],n[1][chroma_pos3],128);
	} else {
		tluma = scalar_interpolate(n[0][x+w*y1],n[0][x+w*y2],n[0][x+w*y3],n[0][x+w*y4]);
		tchromu = scalar_interpolate(n[1][chroma_pos1],n[1][chroma_pos2],n[1][chroma_pos3],n[1][chroma_pos4]);
		tchromv = scalar_interpolate(n[2][chroma_pos1],n[2][chroma_pos2],n[2][chroma_pos3],n[2][chroma_pos4]);
	}

	if (chr == 1 && cwr == 1) {
		m[0][x+ychrcw] = tluma;
	} else {
		m[0][x+y*w] = tluma;
	}
	m[1][xcwr+ychrcw] = tchromu;
	m[2][xcwr+ychrcw] = tchromv;

}

// copies an interlace frame to two half height fields.
static void copy_fields (uint8_t *l[3], uint8_t *m[3], uint8_t *n[3], frame_dimensions *fd ) {

	int h,ch;
	int w,cw;

	int y;

	h = fd->plane_height_luma; // this is the target height
	w = fd->plane_width_luma;

	cw = fd->plane_width_chroma;
	ch = fd->plane_height_chroma;


	for (y=0; y<h; y++) {

		memcpy(&m[0][y*w],&n[0][(y<<1)*w],w);
		memcpy(&l[0][y*w],&n[0][((y<<1)+1)*w],w);

		if (y<ch) {
			memcpy(&m[1][y*cw],&n[1][(y<<1)*cw],cw);
			memcpy(&l[1][y*cw],&n[1][((y<<1)+1)*cw],cw);
			memcpy(&m[2][y*cw],&n[2][(y<<1)*cw],cw);
			memcpy(&l[2][y*cw],&n[2][((y<<1)+1)*cw],cw);
		}

	}
}


static void deint_frame (uint8_t *l[3], uint8_t *m[3], uint8_t *n[3], frame_dimensions *fd ) {

	// detect and interpolate
	// this detection algorithm takes PIXELS vertical pixels and looks for the classic interlace pattern
	// if interlace is detected the *missing* pixels are interpolated.

	int x,y;
	int w,h;
	int mark = 0;
	int full = 0;
	//uint8_t luma[PIXELS];

//	mjpeg_warn("deint_frame");


	h = fd->plane_height_luma;
	w = fd->plane_width_luma;

	chromacpyfd(m,n,fd);
	chromacpyfd(l,n,fd);

	mark = getMark();
	full = getFullframe();

	for (x=0; x<w; x++) {
		for (y=0; y<h; y++) {
			// is interpolation required
			// there may be a more efficient way to de-interlace a full frame
			if (full != 0 || int_detect3(x,y,n,fd) ) {

				switch (mark) {
					case 1:
						mark_deint_pixels(x,y,l,n,fd);
						//mark_deint_pixels(x,y+1,l,n,fd);
						break;
					case 3:
						merge_pixels(x,y,l,n,fd);
//						merge_pixels(x,y+1,l,n,fd);
						break;
					case 5:
						if (!(y%2)) { deint_pixels(x,y,l,n,fd); }
						break;

					default:
						if (!(y%2)) { deint_pixels(x,y,l,n,fd); }
//deint_pixels(x,y+2,l,n,fd);
						if (y%2) { deint_pixels(x,y,m,n,fd); }
//						deint_pixels(x,y+3,m,n,fd);
						break;
				}
			}
		}
	}
}

// it appears that the y4m_si_get functions are chewing up lots'o'cpu

void set_dimensions ( struct frame_dimensions *fd, y4m_stream_info_t  *si)
{

	fd->plane_height_luma = y4m_si_get_plane_height(si,0) ;
	fd->plane_width_luma = y4m_si_get_plane_width(si,0);
	fd->plane_height_chroma = y4m_si_get_plane_height(si,1) ;
	fd->plane_width_chroma = y4m_si_get_plane_width(si,1);
	fd->plane_length_luma = y4m_si_get_plane_length(si,0);
	fd->plane_length_chroma = y4m_si_get_plane_length(si,1);

	fd->chroma_width_ratio = fd->plane_width_luma / fd->plane_width_chroma;
	fd->chroma_height_ratio = fd->plane_height_luma / fd->plane_height_chroma;

}

static void deinterlace(int fdIn,
						y4m_stream_info_t  *inStrInfo,
						int fdOut,
						y4m_stream_info_t  *outStrInfo
						)
{
	y4m_frame_info_t   in_frame ;
	uint8_t            *yuv_data[3] ;
	uint8_t            *yuv_tdata[3] ;
	uint8_t            *yuv_bdata[3] ;
	int                read_error_code ;
	int                write_error_code ;
	int interlaced = Y4M_UNKNOWN;            //=Y4M_ILACE_NONE for not-interlaced scaling, =Y4M_ILACE_TOP_FIRST or Y4M_ILACE_BOTTOM_FIRST for interlaced scaling

	struct frame_dimensions fdim;



	set_dimensions (&fdim, outStrInfo);

	// Allocate memory for the YUV channels
	interlaced = y4m_si_get_interlace(inStrInfo);

	if (chromalloc(yuv_data,inStrInfo))
		mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");

	if (chromalloc(yuv_tdata,outStrInfo))
		mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");

	if (chromalloc(yuv_bdata,outStrInfo))
		mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");

	/* Initialize counters */

	write_error_code = Y4M_OK ;

	y4m_init_frame_info( &in_frame );
	read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_data );

	while( Y4M_ERR_EOF != read_error_code && write_error_code == Y4M_OK ) {

		// de interlace frame.
		if (read_error_code == Y4M_OK) {

			// de-interlace one field to odata and the other in place

			// different behaviour depending on operating mode
			if (getMark() != 2 )
				deint_frame(yuv_bdata, yuv_tdata, yuv_data, &fdim);
			else
				copy_fields(yuv_bdata, yuv_tdata, yuv_data, &fdim);

			//mjpeg_warn("write");


			if (getMark() == 3 || getMark() == 1 || getMark() == 5) {
				write_error_code = y4m_write_frame( fdOut, outStrInfo, &in_frame, yuv_bdata );
			} else if (interlaced == Y4M_ILACE_TOP_FIRST) {
				write_error_code = y4m_write_frame( fdOut, outStrInfo, &in_frame, yuv_tdata );
				write_error_code = y4m_write_frame( fdOut, outStrInfo, &in_frame, yuv_bdata );
			} else {
				write_error_code = y4m_write_frame( fdOut, outStrInfo, &in_frame, yuv_bdata );
				write_error_code = y4m_write_frame( fdOut, outStrInfo, &in_frame, yuv_tdata );
			}

		}

		y4m_fini_frame_info( &in_frame );
		y4m_init_frame_info( &in_frame );
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_data );
	}
	// Clean-up regardless an error happened or not
	y4m_fini_frame_info( &in_frame );

	free( yuv_data[0] );
	free( yuv_data[1] );
	free( yuv_data[2] );
	free( yuv_tdata[0] );
	free( yuv_tdata[1] );
	free( yuv_tdata[2] );
	free( yuv_bdata[0] );
	free( yuv_bdata[1] );
	free( yuv_bdata[2] );

	if( read_error_code != Y4M_ERR_EOF )
		mjpeg_error_exit1 ("Error reading from input stream!");
	if( write_error_code != Y4M_OK )
		mjpeg_error_exit1 ("Error writing output stream!");

}

// Re-interlace a 50p stream into a 25i stream.

static void depro(  int fdIn
				  , y4m_stream_info_t  *inStrInfo
				  , int fdOut
				  , y4m_stream_info_t  *outStrInfo
				  , int proc
				  )
{
	y4m_frame_info_t   in_frame ;
	uint8_t            *yuv_data[3] ;
	uint8_t            *yuv_o1data[3] ;
	uint8_t            *yuv_o2data[3] ;
	int                read_error_code ;
	int                write_error_code ;
	int interlaced = -1;            //=Y4M_ILACE_NONE for not-interlaced scaling, =Y4M_ILACE_TOP_FIRST or Y4M_ILACE_BOTTOM_FIRST for interlaced scaling
	int w,h,y;

	// Allocate memory for the YUV channels
	interlaced = y4m_si_get_interlace(outStrInfo);
	h = y4m_si_get_height(inStrInfo) ; w = y4m_si_get_width(inStrInfo);
	if (chromalloc(yuv_data,inStrInfo))
		mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");

	h = y4m_si_get_height(outStrInfo) ; w = y4m_si_get_width(outStrInfo);
	int frame_data_size = h * w;
	if (chromalloc(yuv_o1data,inStrInfo))
		mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");

	if (chromalloc(yuv_o2data,inStrInfo))
		mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");

	/* Initialize counters */

	write_error_code = Y4M_OK ;

	y4m_init_frame_info( &in_frame );

	if (interlaced == Y4M_ILACE_BOTTOM_FIRST ) {
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o2data );
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o1data );
	} else if (interlaced == Y4M_ILACE_TOP_FIRST) {
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o1data );
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o2data );
	} else {
		mjpeg_warn ("Cannot determine interlace order (assuming top first)\n");
		/* assume top first */
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o1data );
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o2data );
	}


	while( Y4M_ERR_EOF != read_error_code && write_error_code == Y4M_OK ) {

		if (read_error_code == Y4M_OK) {

			//  interlace frame.

			for (y=0; y<h/2; y++)
			{

				/* copy the luminance scan line from the odd frame */
				memcpy(&yuv_data[0][y*2*w],&yuv_o1data[0][y*2*w],w);

				/* copy the alternate luminance scan line from the even frame */
				memcpy(&yuv_data[0][(y*2+1)*w],&yuv_o2data[0][(y*2+1)*w],w);


				/* average the chroma data */
				if (y % 2) {
					// odd bottom field
					//	memcpy (&yuv_data[1][y*w/2],&yuv_o2data[1][y*w/2],w/2);
					//	memcpy (&yuv_data[2][y*w/2],&yuv_o2data[2][y*w/2],w/2);
					int x;
					for (x=0; x<w/2; x++)
					{
						if (proc) {
							yuv_data[1][y*w/2+x] = (yuv_o2data[1][y*w/2+x] + yuv_o1data[1][y*w/2+x]) / 2;
							yuv_data[2][y*w/2+x] = (yuv_o2data[2][y*w/2+x] + yuv_o1data[2][y*w/2+x]) / 2;
						} else {
							yuv_data[1][y*w/2+x] = (yuv_o2data[1][y*w/2+x] + yuv_o2data[1][(y-1)*w/2+x]) /2;
							yuv_data[2][y*w/2+x] = (yuv_o2data[2][y*w/2+x] + yuv_o2data[2][(y-1)*w/2+x]) /2;
						}
					}

				} else {
					// even top field
					//memcpy (&yuv_data[1][y*w/2],&yuv_o1data[1][y*w/2],w/2);
					//memcpy (&yuv_data[2][y*w/2],&yuv_o1data[2][y*w/2],w/2);
					int x;
					for (x=0; x<w/2; x++)
					{
						if (proc) {
							yuv_data[1][y*w/2+x] = (yuv_o2data[1][y*w/2+x] + yuv_o1data[1][y*w/2+x]) / 2;
							yuv_data[2][y*w/2+x] = (yuv_o2data[2][y*w/2+x] + yuv_o1data[2][y*w/2+x]) / 2;
						} else {
							yuv_data[1][y*w/2+x] = (yuv_o1data[1][y*w/2+x] + yuv_o1data[1][(y+1)*w/2+x]) /2;
							yuv_data[2][y*w/2+x] = (yuv_o1data[2][y*w/2+x] + yuv_o1data[2][(y+1)*w/2+x]) /2;
						}
					}
				}
			}

			write_error_code = y4m_write_frame( fdOut, outStrInfo, &in_frame, yuv_data );

		}

		y4m_fini_frame_info( &in_frame );
		y4m_init_frame_info( &in_frame );
		if (interlaced == Y4M_ILACE_BOTTOM_FIRST ) {
			read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o2data );
			read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o1data );
		} else {
			read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o1data );
			read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_o2data );
		}
	}


	// Clean-up regardless an error happened or not
	y4m_fini_frame_info( &in_frame );

	free( yuv_data[0] );
	free( yuv_data[1] );
	free( yuv_data[2] );
	free( yuv_o1data[0] );
	free( yuv_o1data[1] );
	free( yuv_o1data[2] );
	free( yuv_o2data[0] );
	free( yuv_o2data[1] );
	free( yuv_o2data[2] );

	if( read_error_code != Y4M_ERR_EOF )
		mjpeg_error_exit1 ("Error reading from input stream!");
	if( write_error_code != Y4M_OK )
		mjpeg_error_exit1 ("Error writing output stream!");

}


// *************************************************************************************
// MAIN
// *************************************************************************************
int main (int argc, char *argv[])
{

	int verbose = 4 ; //LOG_ERROR
	int fdIn = 0 ;
	int fdOut = 1 ;
	y4m_stream_info_t in_streaminfo, out_streaminfo ;
	y4m_ratio_t frame_rate;
	int interlaced,pro_chroma=0,yuv_interlacing= Y4M_UNKNOWN;
	int height;
	int c ;
	const static char *legal_flags = "I:v:i:pchfm:";

	setFullframe(0);
	setMark(0);
	setInterpolate(0);

	while ((c = getopt (argc, argv, legal_flags)) != -1) {
		switch (c) {
			case 'v':
				verbose = atoi (optarg);
				if (verbose < 0 || verbose > 2)
					mjpeg_error_exit1 ("Verbose level must be [0..2]");
				break;

				case 'h':
				case '?':
				print_usage ();
				return 0 ;
				break;
				case 'f':
				setFullframe(1);
				break;
				case 'I':
				switch (optarg[0]) {
					case 't':  yuv_interlacing = Y4M_ILACE_TOP_FIRST;  break;
					case 'b':  yuv_interlacing = Y4M_ILACE_BOTTOM_FIRST;  break;
					default:
						mjpeg_error("Unknown value for interlace: '%c'", optarg[0]);
						return -1;
						break;
				}
				break;
				case 'i':
				setInterpolate(atoi(optarg));
				break;
				case 'm':
				setMark(atoi(optarg));
				break;
				case 'c':
				pro_chroma = 1;
				break;
		}
	}

	// mjpeg tools global initialisations
	mjpeg_default_handler_verbosity (verbose);

	// Initialize input streams
	y4m_init_stream_info (&in_streaminfo);
	y4m_init_stream_info (&out_streaminfo);

	// ***************************************************************
	// Get video stream informations (size, framerate, interlacing, aspect ratio).
	// The streaminfo structure is filled in
	// ***************************************************************
	// INPUT comes from stdin, we check for a correct file header
	if (y4m_read_stream_header (fdIn, &in_streaminfo) != Y4M_OK)
		mjpeg_error_exit1 ("Could'nt read YUV4MPEG header!");

	// Check input parameters
	height = y4m_si_get_height (&in_streaminfo);
	frame_rate = y4m_si_get_framerate( &in_streaminfo );

	if (yuv_interlacing==Y4M_UNKNOWN) {
		interlaced = y4m_si_get_interlace (&in_streaminfo);
	} else {
		interlaced = yuv_interlacing;
	}

	if ((height%2) && (getMark() != 4))
		mjpeg_error_exit1("material is not even frame height");

	if ((interlaced == Y4M_ILACE_NONE) && (getMark() != 4)) {
		mjpeg_error_exit1("source material not interlaced");
	}


	// set up values for the output stream
	switch (getMark()) {
		case 4:  // progressive to interlace
			frame_rate.d *= 2;
			if (interlaced != Y4M_ILACE_NONE)
				mjpeg_warn("source material is interlaced");
			if (yuv_interlacing != Y4M_ILACE_NONE) {
				mjpeg_warn("No interlace mode selected");
				yuv_interlacing = Y4M_ILACE_BOTTOM_FIRST;
			}
			interlaced = yuv_interlacing;
			break;
		case 2: // half height
			height = height >> 1;
		case 0: // double frame rate
			frame_rate.n *= 2 ;
		case 3: // same height, same frame rate (merge)
		case 5:
			if (yuv_interlacing != Y4M_UNKNOWN)
				y4m_si_set_interlace(&in_streaminfo, yuv_interlacing);

			interlaced = Y4M_ILACE_NONE;
			break;
	}



	// Prepare output stream

	y4m_copy_stream_info( &out_streaminfo, &in_streaminfo );


	// Information output
	mjpeg_info ("yuvdeinterlace (version " YUVDE_VERSION ") is a general deinterlace/interlace utility for yuv streams");
	mjpeg_info ("(C) 2005 Mark Heath <http://silicontrip.net/lavtools/>");
	mjpeg_info ("yuvdeinterlace -h for help");

	y4m_si_set_framerate( &out_streaminfo, frame_rate );
	y4m_si_set_height (&out_streaminfo, height);
	y4m_si_set_interlace(&out_streaminfo, interlaced);
	y4m_write_stream_header(fdOut,&out_streaminfo);

	/* in that function we do all the important work */
	if (getMark() != 4)
		deinterlace( fdIn, &in_streaminfo, fdOut, &out_streaminfo);

	/* this has been replaced by yuvafps */
	if (getMark() == 4)
		depro( fdIn, &in_streaminfo,  fdOut,&out_streaminfo,pro_chroma);

	y4m_fini_stream_info (&in_streaminfo);
	y4m_fini_stream_info (&out_streaminfo);

	return 0;
}
/*
 * Local variables:
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
