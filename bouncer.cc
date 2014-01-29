/* Encodes and decodes a video of a ball bouncing in the 'utah' image format.
*  Interfaces with the FFMPEG library
*/

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>

using namespace std;

#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif
extern "C"
{

#include "../ffmpeg/libavcodec/avcodec.h"
#include "../ffmpeg/libavcodec/utah.h"
#include "../ffmpeg/libavutil/mathematics.h"
#include "../ffmpeg/libavformat/avformat.h"
#include "../ffmpeg/libswscale/swscale.h"
#include "../ffmpeg/libavutil/imgutils.h"
#include "../ffmpeg/libavutil/common.h"

}

/* Draws a pixel on a frame */
void drawPix(int x, int y, int rad, int r, int g, int b, int cx, int cy, AVFrame * f){
	
   uint8_t *pix = f->data[0] + f->linesize[0]*y + x;
	int dx, dy;
	dx = x - cx;
	dy = y - cy;
	double scale = 1 - 0.5*(dx*dx + dy*dy)/(rad *rad);
	*(pix++) = (uint8_t)(scale * r);
	*(pix++) = (uint8_t)(scale * g);
	*(pix++) = (uint8_t)(scale * b);
 
}

/* Draw a ball */
void drawCircle(AVFrame *f, int cx, int cy){
  int rad = f->width/6;
  if(f->height/6 < rad)
    rad = f->height/6;
  int x, y;
  for(y = 0; y < f->height; y++){
    for(x = 0; x < f->width; x++){
      int dx = cx - x;
      int dy = cy - y;
      if(dx*dx + dy*dy < rad*rad)
      drawPix(x, y, rad, 25, 192, 250, cx, cy, f);
    }
  }
}

/* Writes a 'utah' format image */
void WriteUtah (AVCodecContext *pCodecCtx, AVFrame *pFrame, char cFileName[], PixelFormat pix, uint8_t *buffer, int numBytes, int x, int y, int width, int height)
{
  int got_output, i, j;
   AVPacket pkt;
   bool bRet = false;
   AVCodec *pUTAHCodec=NULL;
   pUTAHCodec = avcodec_find_encoder(AV_CODEC_ID_UTAH);
   AVCodecContext *pUTAHCtx = avcodec_alloc_context3(pUTAHCodec);
   FILE *f;

   f = fopen(cFileName, "wb");

   if( pUTAHCtx )
   {
     
      pUTAHCtx->bit_rate = pCodecCtx->bit_rate;
      pUTAHCtx->width = pCodecCtx->width;
      pUTAHCtx->height = pCodecCtx->height;
      pUTAHCtx->pix_fmt = pUTAHCodec->pix_fmts[0];
      pUTAHCtx->codec_id = AV_CODEC_ID_UTAH;
      pUTAHCtx->codec_type = AVMEDIA_TYPE_VIDEO;
     

      if( pUTAHCodec && (avcodec_open2( pUTAHCtx, pUTAHCodec, NULL) >= 0) )
	{
	  pFrame->quality = 10;
	  pFrame->pts = 0;

	  av_init_packet(&pkt);
	  pkt.data = NULL; /* packet data will be allocated by the encoder */
	  pkt.size = 0;

	  int ret = avcodec_encode_video2(pUTAHCtx, &pkt, pFrame, &got_output);

	  if (ret < 0) {
	    fprintf(stderr, "Error encoding frame\n");
	    exit(1);
	  }
	  
	  if (got_output) {
	    fwrite(pkt.data, 1, pkt.size, f);
	    av_free_packet(&pkt);
	  }
	  fclose(f);
	}     
   }
}

AVFrame * copyImage(char *argv,PixelFormat pix, int x, int y)
{
  	
  AVFormatContext *pFormatCtx = NULL;
  int i, videoStream;
  AVCodecContext *pCodecCtx = NULL;
  AVCodec *pCodec = NULL;
  AVFrame *pFrame = NULL;
  AVFrame *pFrameRGB = NULL;
  AVPacket packet;
  int frameFinished;
  int numBytes;
  uint8_t *buffer = NULL;

  AVDictionary *optionsDict = NULL;
  struct SwsContext *sws_ctx = NULL;
  
  /* Register all formats and codecs */
  av_register_all();
  
  /* Open video file */
  if(avformat_open_input(&pFormatCtx, argv, NULL, NULL)!=0)
    return NULL; // Couldn't open file
  
  /* Retrieve stream information */
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return NULL; // Couldn't find stream information
  
  /* Dump information about file onto standard error */
  av_dump_format(pFormatCtx, 0, argv, 0);
  
  /* Find the first video stream */
  videoStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++)
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
      videoStream=i;
      break;
    }
  if(videoStream==-1)
    return NULL; // Didn't find a video stream

  /* Get a pointer to the codec context for the video stream */
  pCodecCtx=pFormatCtx->streams[videoStream]->codec;
  
  /* Find the decoder for the video stream */
  pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
  if(pCodec==NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return NULL; /* Codec not found */
  }
  /* Open codec */
  if(avcodec_open2(pCodecCtx, pCodec, &optionsDict)<0)
    return NULL; /* Could not open codec */
  
  /* Allocate video frame */
  pFrame=avcodec_alloc_frame();
  
  /* Allocate an AVFrame structure */
  pFrameRGB=avcodec_alloc_frame();
  if(pFrameRGB==NULL)
    return NULL;
  
  /* Determine required buffer size and allocate buffer */
  numBytes=avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
  buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

  sws_ctx =
    sws_getContext
    (
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        pix,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );


  /* Assign appropriate parts of buffer to image planes in pFrameRGB
   * Note that pFrameRGB is an AVFrame, but AVFrame is a superset
   * of AVPicture */
  avpicture_fill((AVPicture *)pFrameRGB, buffer, pCodecCtx->pix_fmt,
pCodecCtx->width, pCodecCtx->height);

  /* Read frames and save first five frames to disk */
  i=0;
  while(av_read_frame(pFormatCtx, &packet)>=0) {
    /* Is this a packet from the video stream? */
    if(packet.stream_index==videoStream) {
      /* Decode video frame */
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

      drawCircle(pFrame,x,y);

      /* Did we get a video frame? */
      if(frameFinished) {
	/* Convert the image from its native format to RGB */
        sws_scale
        (
            sws_ctx,
            (uint8_t const * const *)pFrame->data,
            pFrame->linesize,
            0,
            pCodecCtx->height,
            pFrameRGB->data,
            pFrameRGB->linesize
        );      

       return pFrameRGB;
      }
    }
  }

  av_free_packet(&packet);
  
  /* Free the RGB image */
  av_free(buffer);
  
  /* Free the YUV frame */
  av_free(pFrame);
  
  /* Close the codec */
  avcodec_close(pCodecCtx);
  
  /* Close the video file */
  avformat_close_input(&pFormatCtx);
  
  return pFrameRGB;
}



/*Read in a JPG image file*/
int main(int argc, char *argv[]){
  	
  AVFormatContext *pFormatCtx = NULL;
  int i, videoStream;
  AVCodecContext *pCodecCtx = NULL;
  AVCodec *pCodec = NULL;
  AVFrame *pFrame = NULL;
  AVFrame *pFrameRGB = NULL;
  AVPacket packet;
  int frameFinished;
  int numBytes;
  uint8_t *buffer = NULL;
  AVCodec *pUTAHCodec=NULL;
  pUTAHCodec = avcodec_find_encoder(AV_CODEC_ID_UTAH);


  AVDictionary *optionsDict = NULL;
  struct SwsContext *sws_ctx = NULL;
  
  /* Register all formats and codecs */
  av_register_all();
  
  /* Open video file */
  if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0)
    return -1; // Couldn't open file
  
  /* Retrieve stream information */
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; /* Couldn't find stream information */
  
  /* Dump information about file onto standard error */
  av_dump_format(pFormatCtx, 0, argv[1], 0);
  
  /* Find the first video stream */
  videoStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++)
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
      videoStream=i;
      break;
    }
  if(videoStream==-1)
    return -1; /* Didn't find a video stream */
  
  /* Get a pointer to the codec context for the video stream */
  pCodecCtx=pFormatCtx->streams[videoStream]->codec;
  
  /* Find the decoder for the video stream */
  pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
  if(pCodec==NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; /* Codec not found */
  }
  /* Open codec */
  if(avcodec_open2(pCodecCtx, pCodec, &optionsDict)<0)
    return -1; // Could not open codec
  
  /* Allocate video frame */
  pFrame=avcodec_alloc_frame();
  
  /* Allocate an AVFrame structure */
  pFrameRGB=avcodec_alloc_frame();
  if(pFrameRGB==NULL)
    return -1;
  
  /* Determine required buffer size and allocate buffer */
  numBytes=avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
  buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

  sws_ctx =
    sws_getContext
    (
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        pUTAHCodec->pix_fmts[0],
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );


  /* Assign appropriate parts of buffer to image planes in pFrameRGB
   * Note that pFrameRGB is an AVFrame, but AVFrame is a superset
   * of AVPicture */
  avpicture_fill((AVPicture *)pFrameRGB, buffer, pCodecCtx->pix_fmt,
pCodecCtx->width, pCodecCtx->height);

  
  /* Read frames and save first five frames to disk */
  i=0;
  while(av_read_frame(pFormatCtx, &packet)>=0) {
    /* Is this a packet from the video stream? */
    if(packet.stream_index==videoStream) {
      /* Decode video frame */
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
      
      /* Did we get a video frame? */
      if(frameFinished) {
	/* Convert the image from its native format to RGB */
        sws_scale
	  (
	   sws_ctx,
	   (uint8_t const * const *)pFrame->data,
	   pFrame->linesize,
	   0,
	   pCodecCtx->height,
	   pFrameRGB->data,
	   pFrameRGB->linesize
	   );

	int num, thd, pix;
	
	int ball_height, ball_x;

	ball_x = pFrame->width / 2;
   
	for(int i = 0; i <= 300; i++){ 		        
	  char cFileName[32];
	  sprintf(cFileName, "frame%03d.utah", i);
	  thd = pFrame->height / 3;
	  pix = i % 30 - 15;
	  ball_height = (int)(thd + 0.0044 * pix * pix * thd);
	  AVFrame *copy = copyImage(argv[1],pUTAHCodec->pix_fmts[0], ball_x, ball_height);
	  WriteUtah(pCodecCtx, copy, cFileName, PIX_FMT_RGB8, buffer, numBytes, ball_x, ball_height, pCodecCtx->width, pCodecCtx->height);
	}
      }
    }   
    /* Free the packet that was allocated by av_read_frame */
    av_free_packet(&packet);
  }
  
  /* Free the RGB image */
  av_free(buffer);
  
  /* Free the YUV frame */
  av_free(pFrame);
  
  /* Close the codec */
  avcodec_close(pCodecCtx);
  
  /* Close the video file */
  avformat_close_input(&pFormatCtx);
  
  return 0;
  
}




