/*
 *  generic.c
 *    Mark Heath <mjpeg0 at silicontrip.org>
 *  http://silicontrip.net/~mark/lavtools/
 *
** <h3>Bilateral temporal filter</h3>

** <p> performs a temporal bilateral filter over the video. </p>
** <p> As the bilateral filter is edge preserving, this filter has innate motion awareness
** This filter attempts to perform a time based noise reduction while minimising ghosting.</p>

** <p>Based on code from <a href="http://user.cs.tu-berlin.de/~eitz/bilateral_filtering/">m.eitz</a> </p>

** <h4>EXAMPLE</h4>
** <p>Useful options are:</p>
** <pre>
** -D 3
** -R 3
** </pre>
** <p>
** Higher values of R cause more ghosting.  Higher values of D increase the
** search radius, and increase processing time.
** </p>
** <h4>History</h4>
** <p>6-Nov-2011 added y4m accept extensions. To allow for other chroma subsampling.</p>


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
 gcc yuvdeinterlace.c -I/sw/include/mjpegtools -lmjpegutils
 */


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

#define VERSION "0.1"

#define PRECISION 1024

struct parameters {

	unsigned int sigmaD;
	unsigned int sigmaR;

	unsigned int kernelRadius;
	unsigned int kernelSize;


	unsigned int *kernelD;
	unsigned int *kernelR;

	unsigned int *gaussSimilarity;

	unsigned int twoSigmaRSquared;

	int direction;

};

static struct parameters this;


static void print_usage()
{
	fprintf (stderr,
			 "usage: yuvbilateral -r sigmaR -d sigmaD [-v 0..2]\n"
			 "\t -r sigmaR set the similarity distance\n"
			 "\t -r sigmaD set the search radius\n"
			 );
}


unsigned int gauss (unsigned int sigma, int x, int y) {

	// fprintf (stderr,"%f , %d %d\n", (1.0* sigma/PRECISION) ,x,y);

	//float sigmaf =  (1.0* sigma/PRECISION)

	return (exp(-((1.0 * x * x) / (2.0 * (1.0* sigma/PRECISION) * (1.0*sigma/PRECISION))))) * PRECISION;
}

unsigned int similarity(int p, int s) {
	// this equals: Math.exp(-(( Math.abs(p-s)) /  2 * this.sigmaR * this.sigmaR));
	// but is precomputed to improve performance
		return this.gaussSimilarity[abs(p-s)];

}

static void filterinitialize () {

	// int center;
	int x,i;

	this.kernelRadius = this.sigmaD>this.sigmaR?this.sigmaD * 2:this.sigmaR * 2;
	this.kernelRadius = this.kernelRadius / PRECISION;

//	this.twoSigmaRSquared = (2 * (1.0 *this.sigmaR/PRECISION)  * (1.0 *this.sigmaR/PRECISION)) * PRECISION;
	this.twoSigmaRSquared = (2 * (this.sigmaR * this.sigmaR)/PRECISION);


	this.kernelSize = this.kernelRadius * 2 + 1;
	// center = (this.kernelSize - 1) / 2;


	this.kernelD = (unsigned int*) malloc( sizeof (unsigned int) * this.kernelSize );

	if (this.kernelD == NULL ){
		mjpeg_error_exit1("Cannot allocate memory for filter kernel");
	}

// fprintf(stderr,"size %d, radius %d \n",this.kernelSize,this.kernelRadius);

	// x = -center;
	//fprintf (stderr,"x %d < %d\n",x,this.kernelSize - center);


	for ( x=0; x < this.kernelSize ; x++) {
	//	fprintf (stderr,"x %d\n",x);
		this.kernelD[x] = gauss(this.sigmaD, x-this.kernelRadius, 0);
	//fprintf(stderr,"x: %d  = %d\n",x,this.kernelD[x]);
	}

	// fprintf (stderr,"gauss Similarity malloc\n",i);

	this.gaussSimilarity = (unsigned int*) malloc(sizeof (unsigned int) * 256);
	if (this.gaussSimilarity == NULL ){
		free(this.kernelD);
		mjpeg_error_exit1("Cannot allocate memory for gaussian curve");
	}

	// precomute all possible similarity values for
	// performance reasons
	for ( i = 0; i < 256; i++) {
		//fprintf (stderr,"i %d\n",i);
		this.gaussSimilarity[i] = exp(-((i) / (1.0 * this.twoSigmaRSquared/PRECISION))) * PRECISION;
	}


}



static void filterpixel(uint8_t **o, uint8_t ***p,int chan, int i, int j, int w, int h) {

	unsigned int sum =0;
	unsigned int totalWeight = 0;
	int weight;
	int z;
	int pixel_loc;

	pixel_loc = j * w + i;
	uint8_t intensityCenter = p[this.kernelRadius][chan][pixel_loc];

	for ( z = 0; z < this.kernelSize; z++) {

		int intensityKernelPos = p[z][chan][pixel_loc];

		//fprintf (stderr,"kernel D %d. kp %d cen %d\n",this.kernelD[z],intensityKernelPos,intensityCenter);

		// multiplying two fixed precision numbers together squares the PRECISION. So need to remove it.
		weight = this.kernelD[z] * similarity(intensityKernelPos,intensityCenter) / PRECISION;
		totalWeight += weight;
		sum += (weight * intensityKernelPos);

	}
//	fprintf(stderr,"total %d\n",totalWeight);
	if (totalWeight > 0 )
		o[chan][pixel_loc] = sum / totalWeight;
	else
		o[chan][pixel_loc] = intensityCenter;

}

static void filterframe (uint8_t *m[3], uint8_t ***n, y4m_stream_info_t *si)
{

	int x,y;
	int height,width,height2,width2;

	height=y4m_si_get_plane_height(si,0);
	width=y4m_si_get_plane_width(si,0);

	// I'll assume that the chroma subsampling is the same for both u and v channels
	height2=y4m_si_get_plane_height(si,1);
	width2=y4m_si_get_plane_width(si,1);


	for (y=0; y < height; y++) {
		for (x=0; x < width; x++) {

			filterpixel(m,n,0,x,y,width,height);

			if (x<width2 && y<height2) {
				filterpixel(m,n,1,x,y,width2,height2);
				filterpixel(m,n,2,x,y,width2,height2);
			}

		}
	}

}

// temporal filter loop
static void filter(  int fdIn ,int fdOut , y4m_stream_info_t  *inStrInfo )
{
	y4m_frame_info_t   in_frame ;
	uint8_t            ***yuv_data;
	uint8_t				*yuv_odata[3];
	uint8_t				**temp_yuv;
	int                read_error_code ;
	int                write_error_code ;
	int c,d;

	// Allocate memory for the YUV channels
	// may move these off into utility functions
	if (chromalloc(yuv_odata,inStrInfo))
		mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");

	// sanity check!!!
	yuv_data= (uint8_t ***) malloc(sizeof (uint8_t *) * this.kernelSize);
	if (yuv_data == NULL) {
		chromafree(yuv_odata);
		mjpeg_error_exit1("cannot allocate memory for frame buffer");
	}
	for (c=0;c<this.kernelSize;c++) {
		yuv_data[c] = (uint8_t **) malloc(sizeof (uint8_t *) * 3);
		if (yuv_data[c] == NULL) {
			// how am I going to keep track of what I've allocated?
			mjpeg_error_exit1("cannot allocate memory for frame buffer");
		}
		if(chromalloc(yuv_data[c],inStrInfo)) {
			mjpeg_error_exit1("cannot allocate memory for frame buffer");
		}
	}

	/* Initialize counters */

	write_error_code = Y4M_OK ;

	y4m_init_frame_info( &in_frame );
	read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_data[this.kernelSize-1] );

	//init loop mode
	for (c=this.kernelSize-1;c>0;c--) {
		chromacpy(yuv_data[c-1],yuv_data[c],inStrInfo);
	}

	for(d=1;d<this.kernelRadius;d++) {
		y4m_fini_frame_info( &in_frame );
		y4m_init_frame_info( &in_frame );
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_data[this.kernelSize-1] );
		// shuffle
		// should use pointers...

		temp_yuv = yuv_data[0];
		for (c=0;c<this.kernelSize-1;c++) {
	//		chromacpy(yuv_data[c],yuv_data[c+1],inStrInfo);
			yuv_data[c] = yuv_data[c+1];
		}
		yuv_data[this.kernelSize-1] = temp_yuv;

	}

	while( Y4M_ERR_EOF != read_error_code && write_error_code == Y4M_OK ) {

		// do work
		if (read_error_code == Y4M_OK) {
			filterframe(yuv_odata,yuv_data,inStrInfo);
			write_error_code = y4m_write_frame( fdOut, inStrInfo, &in_frame, yuv_odata );
		}

		y4m_fini_frame_info( &in_frame );
		y4m_init_frame_info( &in_frame );
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_data[this.kernelSize-1] );
		// shuffle
		temp_yuv = yuv_data[0];
		for (c=0;c<this.kernelSize-1;c++) {
			// should use pointers...
			//		chromacpy(yuv_data[c],yuv_data[c+1],inStrInfo);
			yuv_data[c] = yuv_data[c+1];
		}
		yuv_data[this.kernelSize-1] = temp_yuv;

	}
	// finalise loop
	for(c=0;c<this.kernelRadius;c++) {
		if (read_error_code == Y4M_OK) {
			filterframe(yuv_odata,yuv_data,inStrInfo);
			write_error_code = y4m_write_frame( fdOut, inStrInfo, &in_frame, yuv_odata );
		}

		y4m_fini_frame_info( &in_frame );
		y4m_init_frame_info( &in_frame );

		// shuffle
		temp_yuv = yuv_data[0];
		for (c=0;c<this.kernelSize-1;c++) {
			// should use pointers...
			//		chromacpy(yuv_data[c],yuv_data[c+1],inStrInfo);
			yuv_data[c] = yuv_data[c+1];
		}
		yuv_data[this.kernelSize-1] = temp_yuv;

	}

	// deallocate temporal buffer

	// Clean-up regardless an error happened or not


	y4m_fini_frame_info( &in_frame );
	chromafree(yuv_odata);
	chromafree(yuv_data);

	if( read_error_code != Y4M_ERR_EOF )
		mjpeg_error_exit1 ("Error reading from input stream!");

}

// *************************************************************************************
// MAIN
// *************************************************************************************
int main (int argc, char *argv[])
{

	int verbose = 4; // LOG_ERROR ;
	int fdIn = 0 ;
	int fdOut = 1 ;
	y4m_stream_info_t in_streaminfo ;
	float sigma;
	int c ;
	const static char *legal_flags = "?hv:r:d:";

	while ((c = getopt (argc, argv, legal_flags)) != -1) {
		switch (c) {
			case 'v':
				verbose = atoi (optarg);
				if (verbose < 0 || verbose > 2)
					mjpeg_error_exit1 ("Verbose level must be [0..2]");
				break;

			case 'h':
			case '?':
				print_usage (argv);
				return 0 ;
				break;
			case 'r':
				sigma = atof(optarg);
				this.sigmaR = sigma * PRECISION;
				break;
			case 'd':
				sigma = atof(optarg);
				this.sigmaD = sigma * PRECISION;
				break;


		}
	}

	if (this.sigmaR == 0 || this.sigmaD == 0) {
		print_usage();
		mjpeg_error_exit1("Sigma D and R must be set");

	}

	y4m_accept_extensions(1);

	// mjpeg tools global initialisations
	mjpeg_default_handler_verbosity (verbose);

	// Initialize input streams
	y4m_init_stream_info (&in_streaminfo);

	// ***************************************************************
	// Get video stream informations (size, framerate, interlacing, aspect ratio).
	// The streaminfo structure is filled in
	// ***************************************************************
	// INPUT comes from stdin, we check for a correct file header
	y4m_accept_extensions(1);
	if (y4m_read_stream_header (fdIn, &in_streaminfo) != Y4M_OK)
		mjpeg_error_exit1 ("Could'nt read YUV4MPEG header!");

	// Information output
	mjpeg_info ("yuvtbilateral (version " VERSION ") is a temporal bilateral filter for yuv streams");
	mjpeg_info ("(C) 2010 Mark Heath <mjpeg0 at silicontrip.org>");
	mjpeg_info (" -h for help");


	/* in that function we do all the important work */
	filterinitialize ();
	y4m_write_stream_header(fdOut,&in_streaminfo);

	filter(fdIn, fdOut, &in_streaminfo);

	y4m_fini_stream_info (&in_streaminfo);

	return 0;
}
/*
 * Local variables:
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
