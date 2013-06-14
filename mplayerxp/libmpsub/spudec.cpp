#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/* SPUdec.c
   Skeleton of function spudec_process_controll() is from xine sources.
   Further works:
   LGB,... (yeah, try to improve it and insert your name here! ;-)

   Kim Minh Kaplan
   implement fragments reassembly, RLE decoding.
   read brightness from the IFO.

   For information on SPU format see <URL:http://sam.zoy.org/doc/dvd/subtitles/>
   and <URL:http://members.aol.com/mpucoder/DVD/spu.html>

 */
#include <algorithm>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "xmpcore/xmp_core.h"
#include "spudec.h"
#include "mpxp_conf_lavc.h"
#include "mpsub_msg.h"

/* Valid values for spu_aamode:
   0: none (fastest, most ugly)
   1: approximate
   2: full (slowest)
   3: bilinear (similiar to vobsub, fast and not too bad)
   4: uses swscaler gaussian (this is the only one that looks good)
 */

int spu_aamode = 3;
int spu_alignment = -1;
float spu_gaussvar = 1.0;

struct packet_t {
  unsigned char *packet;
  unsigned int palette[4];
  unsigned int alpha[4];
  unsigned int control_start;	/* index of start of control data */
  unsigned int current_nibble[2]; /* next data nibble (4 bits) to be
				     processed (for RLE decoding) for
				     even and odd lines */
  int deinterlace_oddness;	/* 0 or 1, index into current_nibble */
  unsigned int start_col, end_col;
  unsigned int start_row, end_row;
  unsigned int width, height, stride;
  unsigned int start_pts, end_pts;
  packet_t *next;
};

struct spudec_handle_t {
  packet_t *queue_head;
  packet_t *queue_tail;
  unsigned int global_palette[16];
  unsigned int orig_frame_width, orig_frame_height;
  unsigned char* packet;
  size_t packet_reserve;	/* size of the memory pointed to by packet */
  unsigned int packet_offset;	/* end of the currently assembled fragment */
  unsigned int packet_size;	/* size of the packet once all fragments are assembled */
  unsigned int packet_pts;	/* PTS for this packet */
  unsigned int palette[4];
  unsigned int alpha[4];
  unsigned int cuspal[4];
  unsigned int custom;
  unsigned int now_pts;
  unsigned int start_pts, end_pts;
  unsigned int start_col, end_col;
  unsigned int start_row, end_row;
  unsigned int width, height, stride;
  size_t image_size;		/* Size of the image buffer */
  unsigned char *image;		/* Grayscale value */
  unsigned char *aimage;	/* Alpha value */
  unsigned int scaled_frame_width, scaled_frame_height;
  unsigned int scaled_start_col, scaled_start_row;
  unsigned int scaled_width, scaled_height, scaled_stride;
  size_t scaled_image_size;
  unsigned char *scaled_image;
  unsigned char *scaled_aimage;
  int auto_palette; /* 1 if we lack a palette and must use an heuristic. */
  int font_start_level;  /* Darkest value used for the computed font */
  int spu_changed;
  unsigned int forced_subs_only;     /* flag: 0=display all subtitle, !0 display only forced subtitles */
  unsigned int is_forced_sub;         /* true if current subtitle is a forced subtitle */
};

static void __FASTCALL__ spudec_queue_packet(spudec_handle_t *self, packet_t *packet)
{
  if (self->queue_head == NULL)
    self->queue_head = packet;
  else
    self->queue_tail->next = packet;
  self->queue_tail = packet;
}

static packet_t __FASTCALL__ *spudec_dequeue_packet(spudec_handle_t *self)
{
  packet_t *retval = self->queue_head;

  self->queue_head = retval->next;
  if (self->queue_head == NULL)
    self->queue_tail = NULL;

  return retval;
}

static void __FASTCALL__ spudec_free_packet(packet_t *packet)
{
  if (packet->packet != NULL) delete packet->packet;
  delete packet;
}

static inline unsigned int __FASTCALL__ get_be16(const unsigned char *p)
{
  return (p[0] << 8) + p[1];
}

static inline unsigned int __FASTCALL__ get_be24(const unsigned char *p)
{
  return (get_be16(p) << 8) + p[2];
}

static void __FASTCALL__ next_line(packet_t *packet)
{
  if (packet->current_nibble[packet->deinterlace_oddness] % 2)
    packet->current_nibble[packet->deinterlace_oddness]++;
  packet->deinterlace_oddness = (packet->deinterlace_oddness + 1) % 2;
}

static inline unsigned char __FASTCALL__ get_nibble(packet_t *packet)
{
  unsigned char nib;
  unsigned int *nibblep = packet->current_nibble + packet->deinterlace_oddness;
  if (*nibblep / 2 >= packet->control_start) {
    mpxp_warn<<"SPUdec: ERROR: get_nibble past end of packet"<<std::endl;
    return 0;
  }
  nib = packet->packet[*nibblep / 2];
  if (*nibblep % 2)
    nib &= 0xf;
  else
    nib >>= 4;
  ++*nibblep;
  return nib;
}

static inline int __FASTCALL__ mkalpha(int i)
{
  /* In mplayer's alpha planes, 0 is transparent, then 1 is nearly
     opaque upto 255 which is transparent */
  switch (i) {
  case 0xf:
    return 1;
  case 0:
    return 0;
  default:
    return (0xf - i) << 4;
  }
}

/* Cut the sub to visible part */
static inline void __FASTCALL__ spudec_cut_image(spudec_handle_t *self)
{
  unsigned int fy, ly;
  unsigned int first_y, last_y;
  unsigned char *image;
  unsigned char *aimage;

  if (self->stride == 0 || self->height == 0) {
    return;
  }

  for (fy = 0; fy < self->image_size && !self->aimage[fy]; fy++);
  for (ly = self->stride * self->height-1; ly && !self->aimage[ly]; ly--);
  first_y = fy / self->stride;
  last_y = ly / self->stride;
  //printf("first_y: %d, last_y: %d\n", first_y, last_y);
  self->start_row += first_y;

  // Some subtitles trigger self condition
  if (last_y + 1 > first_y ) {
	  self->height = last_y - first_y +1;
  } else {
	  self->height = 0;
	  self->image_size = 0;
	  return;
  }

//  printf("new h %d new start %d (sz %d st %d)---\n\n", self->height, self->start_row, self->image_size, self->stride);

  image = new unsigned char [2 * self->stride * self->height];
  if(image){
    self->image_size = self->stride * self->height;
    aimage = image + self->image_size;
    memcpy(image, self->image + self->stride * first_y, self->image_size);
    memcpy(aimage, self->aimage + self->stride * first_y, self->image_size);
    delete self->image;
    self->image = image;
    self->aimage = aimage;
  } else {
    mpxp_fatal<<"Fatal: update_spu: mp_malloc requested "<<(2*self->stride*self->height)<<" bytes"<<std::endl;
  }
}

static void __FASTCALL__ spudec_process_data(spudec_handle_t *self, packet_t *packet)
{
  unsigned int cmap[4], alpha[4];
  unsigned int i, x, y;

  self->scaled_frame_width = 0;
  self->scaled_frame_height = 0;
  self->start_col = packet->start_col;
  self->end_col = packet->end_col;
  self->start_row = packet->start_row;
  self->end_row = packet->end_row;
  self->height = packet->height;
  self->width = packet->width;
  self->stride = packet->stride;
  for (i = 0; i < 4; ++i) {
    alpha[i] = mkalpha(packet->alpha[i]);
    if (alpha[i] == 0)
      cmap[i] = 0;
    else if (self->custom){
      cmap[i] = ((self->cuspal[i] >> 16) & 0xff);
      if (cmap[i] + alpha[i] > 255)
	cmap[i] = 256 - alpha[i];
    }
    else {
      cmap[i] = ((self->global_palette[packet->palette[i]] >> 16) & 0xff);
      if (cmap[i] + alpha[i] > 255)
	cmap[i] = 256 - alpha[i];
    }
  }

  if (self->image_size < self->stride * self->height) {
    if (self->image != NULL) {
      delete self->image;
      self->image_size = 0;
    }
    self->image = new unsigned char [2 * self->stride * self->height];
    if (self->image) {
      self->image_size = self->stride * self->height;
      self->aimage = self->image + self->image_size;
    }
  }
  if (self->image == NULL)
    return;

  /* Kludge: draw_alpha needs width multiple of 8. */
  if (self->width < self->stride)
    for (y = 0; y < self->height; ++y)
      memset(self->aimage + y * self->stride + self->width, 0, self->stride - self->width);

  i = packet->current_nibble[1];
  x = 0;
  y = 0;
  while (packet->current_nibble[0] < i
	 && packet->current_nibble[1] / 2 < packet->control_start
	 && y < self->height) {
    unsigned int len, color;
    unsigned int rle = 0;
    rle = get_nibble(packet);
    if (rle < 0x04) {
      rle = (rle << 4) | get_nibble(packet);
      if (rle < 0x10) {
	rle = (rle << 4) | get_nibble(packet);
	if (rle < 0x040) {
	  rle = (rle << 4) | get_nibble(packet);
	  if (rle < 0x0004)
	    rle |= ((self->width - x) << 2);
	}
      }
    }
    color = 3 - (rle & 0x3);
    len = rle >> 2;
    if (len > self->width - x || len == 0)
      len = self->width - x;
    /* FIXME have to use palette and alpha map*/
    memset(self->image + y * self->stride + x, cmap[color], len);
    memset(self->aimage + y * self->stride + x, alpha[color], len);
    x += len;
    if (x >= self->width) {
      next_line(packet);
      x = 0;
      ++y;
    }
  }
  spudec_cut_image(self);
}


/*
  This function tries to create a usable palette.
  It determines how many non-transparent colors are used, and assigns different
gray scale values to each color.
  I tested it with four streams and even got something readable. Half of the
times I got black characters with white around and half the reverse.
*/
static void __FASTCALL__ compute_palette(spudec_handle_t *self, packet_t *packet)
{
  int used[16],i,cused,start,step,color;

  memset(used, 0, sizeof(used));
  for (i=0; i<4; i++)
    if (packet->alpha[i]) /* !Transparent? */
       used[packet->palette[i]] = 1;
  for (cused=0, i=0; i<16; i++)
    if (used[i]) cused++;
  if (!cused) return;
  if (cused == 1) {
    start = 0x80;
    step = 0;
  } else {
    start = self->font_start_level;
    step = (0xF0-self->font_start_level)/(cused-1);
  }
  memset(used, 0, sizeof(used));
  for (i=0; i<4; i++) {
    color = packet->palette[i];
    if (packet->alpha[i] && !used[color]) { /* not assigned? */
       used[color] = 1;
       self->global_palette[color] = start<<16;
       start += step;
    }
  }
}

static void __FASTCALL__ spudec_process_control(spudec_handle_t *self, unsigned int pts100)
{
  int a,b; /* Temporary vars */
  unsigned int date, type;
  unsigned int off;
  unsigned int start_off = 0;
  unsigned int next_off;
  unsigned int start_pts=0;
  unsigned int end_pts=0;
  unsigned int current_nibble[2] = { 0, 0 };
  unsigned int control_start;
  unsigned int display = 0;
  unsigned int start_col = 0;
  unsigned int end_col = 0;
  unsigned int start_row = 0;
  unsigned int end_row = 0;
  unsigned int width = 0;
  unsigned int height = 0;
  unsigned int stride = 0;

  control_start = get_be16(self->packet + 2);
  next_off = control_start;
  while (start_off != next_off) {
    start_off = next_off;
    date = get_be16(self->packet + start_off) * 1024;
    next_off = get_be16(self->packet + start_off + 2);
    mpxp_dbg2<<"date="<<date<<std::endl;
    off = start_off + 4;
    for (type = self->packet[off++]; type != 0xff; type = self->packet[off++]) {
      mpxp_dbg2<<"cmd="<<type<<std::endl;
      switch(type) {
      case 0x00:
	/* Menu ID, 1 byte */
	mpxp_dbg2<<"Menu ID"<<std::endl;
	/* shouldn't a Menu ID type force display start? */
	start_pts = pts100 + date;
	end_pts = UINT_MAX;
	display = 1;
	self->is_forced_sub=~0; // current subtitle is forced
	break;
      case 0x01:
	/* Start display */
	mpxp_dbg2<<"Start display!"<<std::endl;
	start_pts = pts100 + date;
	end_pts = UINT_MAX;
	display = 1;
	self->is_forced_sub=0;
	break;
      case 0x02:
	/* Stop display */
	mpxp_dbg2<<"Stop display!"<<std::endl;
	end_pts = pts100 + date;
	break;
      case 0x03:
	/* Palette */
	self->palette[0] = self->packet[off] >> 4;
	self->palette[1] = self->packet[off] & 0xf;
	self->palette[2] = self->packet[off + 1] >> 4;
	self->palette[3] = self->packet[off + 1] & 0xf;
	mpxp_dbg2<<"Palette "<<self->palette[0]<<", "<<self->palette[1]<<", "<<self->palette[2]<<", "<<self->palette[3]<<std::endl;
	off+=2;
	break;
      case 0x04:
	/* Alpha */
	self->alpha[0] = self->packet[off] >> 4;
	self->alpha[1] = self->packet[off] & 0xf;
	self->alpha[2] = self->packet[off + 1] >> 4;
	self->alpha[3] = self->packet[off + 1] & 0xf;
	mpxp_dbg2<<"Alpha "<<self->alpha[0]<<", "<<self->alpha[1]<<", "<<self->alpha[2]<<", "<<self->alpha[3]<<std::endl;
	off+=2;
	break;
      case 0x05:
	/* Co-ords */
	a = get_be24(self->packet + off);
	b = get_be24(self->packet + off + 3);
	start_col = a >> 12;
	end_col = a & 0xfff;
	width = (end_col < start_col) ? 0 : end_col - start_col + 1;
	stride = (width + 7) & ~7; /* Kludge: draw_alpha needs width multiple of 8 */
	start_row = b >> 12;
	end_row = b & 0xfff;
	height = (end_row < start_row) ? 0 : end_row - start_row /* + 1 */;
	mpxp_dbg2<<"Coords  col: "<<start_col<<" - "<<end_col<<"  row: "<<start_row<<" - "<<end_row<<"  ("<<width<<"x"<<height<<")"<<std::endl;
	off+=6;
	break;
      case 0x06:
	/* Graphic lines */
	current_nibble[0] = 2 * get_be16(self->packet + off);
	current_nibble[1] = 2 * get_be16(self->packet + off + 2);
	mpxp_dbg2<<"Graphic offset 1: "<<(current_nibble[0] / 2)<<"  offset 2: "<<(current_nibble[1] / 2)<<std::endl;
	off+=4;
	break;
      case 0xff:
	/* All done, bye-bye */
	mpxp_dbg2<<"Done!"<<std::endl;
	return;
//	break;
      default:
	mpxp_warn<<"spudec: Error determining control type 0x"<<std::hex<<type<<".  Skipping "<<(next_off - off)<<" bytes"<<std::endl;
	goto next_control;
      }
    }
  next_control:
    if (display) {
      packet_t *packet = new(zeromem) packet_t;
      int i;
      packet->start_pts = start_pts;
      if (end_pts == UINT_MAX && start_off != next_off) {
	start_pts = pts100 + get_be16(self->packet + next_off) * 1024;
	packet->end_pts = start_pts - 1;
      } else packet->end_pts = end_pts;
      packet->current_nibble[0] = current_nibble[0];
      packet->current_nibble[1] = current_nibble[1];
      packet->start_row = start_row;
      packet->end_row = end_row;
      packet->start_col = start_col;
      packet->end_col = end_col;
      packet->width = width;
      packet->height = height;
      packet->stride = stride;
      packet->control_start = control_start;
      for (i=0; i<4; i++) {
	packet->alpha[i] = self->alpha[i];
	packet->palette[i] = self->palette[i];
      }
      packet->packet = new unsigned char [self->packet_size];
      memcpy(packet->packet, self->packet, self->packet_size);
      spudec_queue_packet(self, packet);
    }
  }
}

static void __FASTCALL__ spudec_decode(spudec_handle_t *self, unsigned int pts100)
{
    spudec_process_control(self, pts100);
}

int __FASTCALL__ spudec_changed(any_t* self)
{
    spudec_handle_t * spu = (spudec_handle_t*)self;
    return (spu->spu_changed || spu->now_pts > spu->end_pts);
}

void __FASTCALL__ spudec_assemble(any_t*self, unsigned char *packet, unsigned int len, unsigned int pts100)
{
  spudec_handle_t *spu = (spudec_handle_t*)self;
//  spudec_heartbeat(self, pts100);
  if (len < 2) {
      mpxp_warn<<"SPUasm: packet too short"<<std::endl;
      return;
  }
  spu->packet_pts = pts100;
  if (spu->packet_offset == 0) {
    unsigned int len2 = get_be16(packet);
    // Start new fragment
    if (spu->packet_reserve < len2) {
      if (spu->packet != NULL) delete spu->packet;
      spu->packet = new unsigned char [len2];
      spu->packet_reserve = spu->packet != NULL ? len2 : 0;
    }
    if (spu->packet != NULL) {
      spu->packet_size = len2;
      if (len > len2) {
	mpxp_warn<<"SPUasm: invalid frag len / len2: "<<len<<" / "<<len2<<std::endl;
	return;
      }
      memcpy(spu->packet, packet, len);
      spu->packet_offset = len;
      spu->packet_pts = pts100;
    }
  } else {
    // Continue current fragment
    if (spu->packet_size < spu->packet_offset + len){
      mpxp_warn<<"SPUasm: invalid fragment"<<std::endl;
      spu->packet_size = spu->packet_offset = 0;
      return;
    } else {
      memcpy(spu->packet + spu->packet_offset, packet, len);
      spu->packet_offset += len;
    }
  }
#if 1
  // check if we have a complete packet (unfortunatelly packet_size is bad
  // for some disks)
  // [cb] packet_size is padded to be even -> may be one byte too long
  if ((spu->packet_offset == spu->packet_size) ||
      ((spu->packet_offset + 1) == spu->packet_size)){
    unsigned int x=0,y;
    while(x+4<=spu->packet_offset){
      y=get_be16(spu->packet+x+2); // next control pointer
      mpxp_dbg2<<"SPUtest: x="<<x<<" y="<<y<<" off="<<spu->packet_offset<<" size="<<spu->packet_size<<std::endl;
      if(x>=4 && x==y){		// if it points to self - we're done!
	// we got it!
	mpxp_dbg2<<"SPUgot: off="<<spu->packet_offset<<" size="<<spu->packet_size<<std::endl;
	spudec_decode(spu, pts100);
	spu->packet_offset = 0;
	break;
      }
      if(y<=x || y>=spu->packet_size){ // invalid?
	mpxp_warn<<"SPUtest: broken packet!!!!! y="<<y<<" < x="<<x<<std::endl;
	spu->packet_size = spu->packet_offset = 0;
	break;
      }
      x=y;
    }
    // [cb] packet is done; start new packet
    spu->packet_offset = 0;
  }
#else
  if (spu->packet_offset == spu->packet_size) {
    spudec_decode(spu, pts100);
    spu->packet_offset = 0;
  }
#endif
}

void __FASTCALL__ spudec_reset(any_t*self)	// called after seek
{
  spudec_handle_t *spu = (spudec_handle_t*)self;
  while (spu->queue_head)
    spudec_free_packet(spudec_dequeue_packet(spu));
  spu->now_pts = 0;
  spu->end_pts = 0;
  spu->packet_size = spu->packet_offset = 0;
}

void __FASTCALL__ spudec_now_pts(any_t*self, unsigned int pts100)
{
  spudec_handle_t *spu = (spudec_handle_t*) self;
  spu->now_pts = pts100;
}

void __FASTCALL__ spudec_heartbeat(any_t*self, unsigned int pts100)
{
  spudec_handle_t *spu = (spudec_handle_t*) self;
  spu->now_pts = pts100;

  while (spu->queue_head != NULL && pts100 >= spu->queue_head->start_pts) {
    packet_t *packet = spudec_dequeue_packet(spu);
    spu->start_pts = packet->start_pts;
    spu->end_pts = packet->end_pts;
    if (spu->auto_palette)
      compute_palette(spu, packet);
    spudec_process_data(spu, packet);
    spudec_free_packet(packet);
    spu->spu_changed = 1;
  }
}

int __FASTCALL__ spudec_visible(any_t*self){
    spudec_handle_t *spu = (spudec_handle_t *)self;
    int ret=(spu->start_pts <= spu->now_pts &&
	     spu->now_pts < spu->end_pts &&
	     spu->height > 0);
    return ret;
}

void __FASTCALL__ spudec_set_forced_subs_only(any_t* const self, const unsigned int flag)
{
  if(self){
      ((spudec_handle_t *)self)->forced_subs_only=flag;
      mpxp_dbg2<<"SPU: Display only forced subs now "<<(flag ? "enabled": "disabled")<<std::endl;
  }
}

void __FASTCALL__ spudec_draw(any_t*self, draw_osd_f draw_alpha,const Video_Output*vo)
{
    spudec_handle_t *spu = (spudec_handle_t *)self;
    if (spu->start_pts <= spu->now_pts && spu->now_pts < spu->end_pts && spu->image)
    {
	draw_alpha(vo,dae_curr_vdecoded(mpxp_context().engine().xp_core),spu->start_col, spu->start_row, spu->width, spu->height,
		   spu->image, spu->aimage, spu->stride);
	spu->spu_changed = 0;
    }
}

/* calc the bbox for spudec subs */
void __FASTCALL__ spudec_calc_bbox(any_t*me, unsigned int dxs, unsigned int dys, unsigned int* bbox)
{
  spudec_handle_t *spu;
  spu = (spudec_handle_t *)me;
  if (spu->orig_frame_width == 0 || spu->orig_frame_height == 0
  || (spu->orig_frame_width == dxs && spu->orig_frame_height == dys)) {
    bbox[0] = spu->start_col;
    bbox[1] = spu->start_col + spu->width;
    bbox[2] = spu->start_row;
    bbox[3] = spu->start_row + spu->height;
  }
  else if (spu->scaled_frame_width != dxs || spu->scaled_frame_height != dys) {
    unsigned int scalex = 0x100 * dxs / spu->orig_frame_width;
    unsigned int scaley = 0x100 * dys / spu->orig_frame_height;
    bbox[0] = spu->start_col * scalex / 0x100;
    bbox[1] = spu->start_col * scalex / 0x100 + spu->width * scalex / 0x100;
    switch (spu_alignment) {
    case 0:
      bbox[3] = dys*sub_data.pos/100 + spu->height * scaley / 0x100;
      if (bbox[3] > dys) bbox[3] = dys;
      bbox[2] = bbox[3] - spu->height * scaley / 0x100;
      break;
    case 1:
      if (sub_data.pos < 50) {
	bbox[2] = dys*sub_data.pos/100 - spu->height * scaley / 0x200;
	if ((int)(bbox[2]) < 0) bbox[2] = 0;
	bbox[3] = bbox[2] + spu->height;
      } else {
	bbox[3] = dys*sub_data.pos/100 + spu->height * scaley / 0x200;
	if (bbox[3] > dys) bbox[3] = dys;
	bbox[2] = bbox[3] - spu->height * scaley / 0x100;
      }
      break;
    case 2:
      bbox[2] = dys*sub_data.pos/100 - spu->height * scaley / 0x100;
      if ((int)(bbox[2]) < 0) bbox[2] = 0;
      bbox[3] = bbox[2] + spu->height;
      break;
    default: /* -1 */
      bbox[2] = spu->start_row * scaley / 0x100;
      bbox[3] = spu->start_row * scaley / 0x100 + spu->height * scaley / 0x100;
      break;
    }
  }
}

/* transform mplayer's alpha value into an opacity value that is linear */
static inline int __FASTCALL__ canon_alpha(int alpha)
{
  return alpha ? 256 - alpha : 0;
}

struct scale_pixel {
  unsigned position;
  unsigned left_up;
  unsigned right_down;
};

static void  __FASTCALL__ scale_table(unsigned int start_src, unsigned int start_tar, unsigned int end_src, unsigned int end_tar, scale_pixel * table)
{
  unsigned int t;
  unsigned int delta_src = end_src - start_src;
  unsigned int delta_tar = end_tar - start_tar;
  unsigned int src = 0;
  unsigned int src_step;
  if (delta_src == 0 || delta_tar == 0) {
    return;
  }
  src_step = (delta_src << 16) / delta_tar >>1;
  for (t = 0; t<=delta_tar; src += (src_step << 1), t++){
    table[t].position= std::min(src >> 16, end_src - 1);
    table[t].right_down = src & 0xffff;
    table[t].left_up = 0x10000 - table[t].right_down;
  }
}

/* bilinear scale, similar to vobsub's code */
static void  __FASTCALL__ scale_image(int x, int y, scale_pixel* table_x, scale_pixel* table_y, spudec_handle_t * spu)
{
  int alpha[4];
  int color[4];
  unsigned int scale[4];
  int base = table_y[y].position * spu->stride + table_x[x].position;
  int scaled = y * spu->scaled_stride + x;
  alpha[0] = canon_alpha(spu->aimage[base]);
  alpha[1] = canon_alpha(spu->aimage[base + 1]);
  alpha[2] = canon_alpha(spu->aimage[base + spu->stride]);
  alpha[3] = canon_alpha(spu->aimage[base + spu->stride + 1]);
  color[0] = spu->image[base];
  color[1] = spu->image[base + 1];
  color[2] = spu->image[base + spu->stride];
  color[3] = spu->image[base + spu->stride + 1];
  scale[0] = (table_x[x].left_up * table_y[y].left_up >> 16) * alpha[0];
  scale[1] = (table_x[x].right_down * table_y[y].left_up >>16) * alpha[1];
  scale[2] = (table_x[x].left_up * table_y[y].right_down >> 16) * alpha[2];
  scale[3] = (table_x[x].right_down * table_y[y].right_down >> 16) * alpha[3];
  spu->scaled_image[scaled] = (color[0] * scale[0] + color[1] * scale[1] + color[2] * scale[2] + color[3] * scale[3])>>24;
  spu->scaled_aimage[scaled] = (scale[0] + scale[1] + scale[2] + scale[3]) >> 16;
  if (spu->scaled_aimage[scaled]){
    spu->scaled_aimage[scaled] = 256 - spu->scaled_aimage[scaled];
    if(spu->scaled_aimage[scaled] + spu->scaled_image[scaled] > 255)
      spu->scaled_image[scaled] = 256 - spu->scaled_aimage[scaled];
  }
}

void  __FASTCALL__ sws_spu_image(unsigned char *d1, unsigned char *d2, int dw, int dh, int ds,
	unsigned char *s1, unsigned char *s2, int sw, int sh, int ss)
{
	struct SwsContext *ctx;
	static SwsFilter filter;
	static int firsttime = 1;
	static float oldvar;
	int i;

	if (!firsttime && oldvar != spu_gaussvar) sws_freeVec(filter.lumH);
	if (firsttime) {
		filter.lumH = filter.lumV =
			filter.chrH = filter.chrV = sws_getGaussianVec(spu_gaussvar, 3.0);
		sws_normalizeVec(filter.lumH, 1.0);
		firsttime = 0;
		oldvar = spu_gaussvar;
	}

	ctx=sws_getContext(sw, sh, pixfmt_from_fourcc(IMGFMT_Y800), dw, dh, pixfmt_from_fourcc(IMGFMT_Y800), SWS_GAUSS, &filter, NULL, NULL);
	sws_scale(ctx,&s1,&ss,0,sh,&d1,&ds);
	for (i=ss*sh-1; i>=0; i--) if (!s2[i]) s2[i] = 255; //else s2[i] = 1;
	sws_scale(ctx,&s2,&ss,0,sh,&d2,&ds);
	for (i=ds*dh-1; i>=0; i--) if (d2[i]==0) d2[i] = 1; else if (d2[i]==255) d2[i] = 0;
	sws_freeContext(ctx);
}

void __FASTCALL__ spudec_draw_scaled(any_t*me, unsigned int dxs, unsigned int dys, draw_osd_f draw_alpha,const Video_Output*vo)
{
  spudec_handle_t *spu = (spudec_handle_t *)me;
  scale_pixel *table_x;
  scale_pixel *table_y;

  if (spu->start_pts <= spu->now_pts && spu->now_pts < spu->end_pts) {

    // check if only forced subtitles are requested
    if( (spu->forced_subs_only) && !(spu->is_forced_sub) ){
	return;
    }

    if (!(spu_aamode&16) && (spu->orig_frame_width == 0 || spu->orig_frame_height == 0
	|| (spu->orig_frame_width == dxs && spu->orig_frame_height == dys))) {
      if (spu->image)
      {
	draw_alpha(vo,dae_curr_vdecoded(mpxp_context().engine().xp_core),spu->start_col, spu->start_row, spu->width, spu->height,
		   spu->image, spu->aimage, spu->stride);
	spu->spu_changed = 0;
      }
    }
    else {
      if (spu->scaled_frame_width != dxs || spu->scaled_frame_height != dys) {	/* Resizing is needed */
	/* scaled_x = scalex * x / 0x100
	   scaled_y = scaley * y / 0x100
	   order of operations is important because of rounding. */
	unsigned int scalex = 0x100 * dxs / spu->orig_frame_width;
	unsigned int scaley = 0x100 * dys / spu->orig_frame_height;
	spu->scaled_start_col = spu->start_col * scalex / 0x100;
	spu->scaled_start_row = spu->start_row * scaley / 0x100;
	spu->scaled_width = spu->width * scalex / 0x100;
	spu->scaled_height = spu->height * scaley / 0x100;
	/* Kludge: draw_alpha needs width multiple of 8 */
	spu->scaled_stride = (spu->scaled_width + 7) & ~7;
	if (spu->scaled_image_size < spu->scaled_stride * spu->scaled_height) {
	  if (spu->scaled_image) {
	    delete spu->scaled_image;
	    spu->scaled_image_size = 0;
	  }
	  spu->scaled_image = new unsigned char [2 * spu->scaled_stride * spu->scaled_height];
	  if (spu->scaled_image) {
	    spu->scaled_image_size = spu->scaled_stride * spu->scaled_height;
	    spu->scaled_aimage = spu->scaled_image + spu->scaled_image_size;
	  }
	}
	if (spu->scaled_image) {
	  unsigned int x, y;
	  if (spu->scaled_width <= 1 || spu->scaled_height <= 1) {
	    goto nothing_to_do;
	  }
	  switch(spu_aamode&15) {
	  case 4:
	  sws_spu_image(spu->scaled_image, spu->scaled_aimage,
		  spu->scaled_width, spu->scaled_height, spu->scaled_stride,
		  spu->image, spu->aimage, spu->width, spu->height, spu->stride);
	  break;
	  case 3:
	  table_x = new(zeromem) scale_pixel[spu->scaled_width];
	  table_y = new(zeromem) scale_pixel[spu->scaled_height];
	  if (!table_x || !table_y) {
	    mpxp_fatal<<"Fatal: spudec_draw_scaled: mp_calloc failed"<<std::endl;
	  }
	  scale_table(0, 0, spu->width - 1, spu->scaled_width - 1, table_x);
	  scale_table(0, 0, spu->height - 1, spu->scaled_height - 1, table_y);
	  for (y = 0; y < spu->scaled_height; y++)
	    for (x = 0; x < spu->scaled_width; x++)
	      scale_image(x, y, table_x, table_y, spu);
	  delete table_x;
	  delete table_y;
	  break;
	  case 0:
	  /* no antialiasing */
	  for (y = 0; y < spu->scaled_height; ++y) {
	    int unscaled_y = y * 0x100 / scaley;
	    int strides = spu->stride * unscaled_y;
	    int scaled_strides = spu->scaled_stride * y;
	    for (x = 0; x < spu->scaled_width; ++x) {
	      int unscaled_x = x * 0x100 / scalex;
	      spu->scaled_image[scaled_strides + x] = spu->image[strides + unscaled_x];
	      spu->scaled_aimage[scaled_strides + x] = spu->aimage[strides + unscaled_x];
	    }
	  }
	  break;
	  case 1:
	  {
	    /* Intermediate antialiasing. */
	    for (y = 0; y < spu->scaled_height; ++y) {
	      const unsigned int unscaled_top = y * spu->orig_frame_height / dys;
	      unsigned int unscaled_bottom = (y + 1) * spu->orig_frame_height / dys;
	      if (unscaled_bottom >= spu->height)
		unscaled_bottom = spu->height - 1;
	      for (x = 0; x < spu->scaled_width; ++x) {
		const unsigned int unscaled_left = x * spu->orig_frame_width / dxs;
		unsigned int unscaled_right = (x + 1) * spu->orig_frame_width / dxs;
		unsigned int color = 0;
		unsigned int alpha = 0;
		unsigned int walkx, walky;
		unsigned int base, tmp;
		if (unscaled_right >= spu->width)
		  unscaled_right = spu->width - 1;
		for (walky = unscaled_top; walky <= unscaled_bottom; ++walky)
		  for (walkx = unscaled_left; walkx <= unscaled_right; ++walkx) {
		    base = walky * spu->stride + walkx;
		    tmp = canon_alpha(spu->aimage[base]);
		    alpha += tmp;
		    color += tmp * spu->image[base];
		  }
		base = y * spu->scaled_stride + x;
		spu->scaled_image[base] = alpha ? color / alpha : 0;
		spu->scaled_aimage[base] =
		  alpha * (1 + unscaled_bottom - unscaled_top) * (1 + unscaled_right - unscaled_left);
		/* spu->scaled_aimage[base] =
		  alpha * dxs * dys / spu->orig_frame_width / spu->orig_frame_height; */
		if (spu->scaled_aimage[base]) {
		  spu->scaled_aimage[base] = 256 - spu->scaled_aimage[base];
		  if (spu->scaled_aimage[base] + spu->scaled_image[base] > 255)
		    spu->scaled_image[base] = 256 - spu->scaled_aimage[base];
		}
	      }
	    }
	  }
	  break;
	  case 2:
	  {
	    /* Best antialiasing.  Very slow. */
	    /* Any pixel (x, y) represents pixels from the original
	       rectangular region comprised between the columns
	       unscaled_y and unscaled_y + 0x100 / scaley and the rows
	       unscaled_x and unscaled_x + 0x100 / scalex

	       The original rectangular region that the scaled pixel
	       represents is cut in 9 rectangular areas like self:

	       +---+-----------------+---+
	       | 1 |        2        | 3 |
	       +---+-----------------+---+
	       |   |                 |   |
	       | 4 |        5        | 6 |
	       |   |                 |   |
	       +---+-----------------+---+
	       | 7 |        8        | 9 |
	       +---+-----------------+---+

	       The width of the left column is at most one pixel and
	       it is never null and its right column is at a pixel
	       boundary.  The height of the top row is at most one
	       pixel it is never null and its bottom row is at a
	       pixel boundary. The width and height of region 5 are
	       integral values.  The width of the right column is
	       what remains and is less than one pixel.  The height
	       of the bottom row is what remains and is less than
	       one pixel.

	       The row above 1, 2, 3 is unscaled_y.  The row between
	       1, 2, 3 and 4, 5, 6 is top_low_row.  The row between 4,
	       5, 6 and 7, 8, 9 is (unsigned int)unscaled_y_bottom.
	       The row beneath 7, 8, 9 is unscaled_y_bottom.

	       The column left of 1, 4, 7 is unscaled_x.  The column
	       between 1, 4, 7 and 2, 5, 8 is left_right_column.  The
	       column between 2, 5, 8 and 3, 6, 9 is (unsigned
	       int)unscaled_x_right.  The column right of 3, 6, 9 is
	       unscaled_x_right. */
	    const double inv_scalex = (double) 0x100 / scalex;
	    const double inv_scaley = (double) 0x100 / scaley;
	    for (y = 0; y < spu->scaled_height; ++y) {
	      const double unscaled_y = y * inv_scaley;
	      const double unscaled_y_bottom = unscaled_y + inv_scaley;
	      const unsigned int top_low_row = std::min(unscaled_y_bottom, unscaled_y + 1.0);
	      const double top = top_low_row - unscaled_y;
	      const unsigned int height = unscaled_y_bottom > top_low_row
		? (unsigned int) unscaled_y_bottom - top_low_row
		: 0;
	      const double bottom = unscaled_y_bottom > top_low_row
		? unscaled_y_bottom - floor(unscaled_y_bottom)
		: 0.0;
	      for (x = 0; x < spu->scaled_width; ++x) {
		const double unscaled_x = x * inv_scalex;
		const double unscaled_x_right = unscaled_x + inv_scalex;
		const unsigned int left_right_column = std::min(unscaled_x_right, unscaled_x + 1.0);
		const double left = left_right_column - unscaled_x;
		const unsigned int width = unscaled_x_right > left_right_column
		  ? (unsigned int) unscaled_x_right - left_right_column
		  : 0;
		const double right = unscaled_x_right > left_right_column
		  ? unscaled_x_right - floor(unscaled_x_right)
		  : 0.0;
		double color = 0.0;
		double alpha = 0.0;
		double tmp;
		unsigned int base;
		/* Now use these informations to compute a good alpha,
		   and lightness.  The sum is on each of the 9
		   region's surface and alpha and lightness.

		  transformed alpha = sum(surface * alpha) / sum(surface)
		  transformed color = sum(surface * alpha * color) / sum(surface * alpha)
		*/
		/* 1: top left part */
		base = spu->stride * (unsigned int) unscaled_y;
		tmp = left * top * canon_alpha(spu->aimage[base + (unsigned int) unscaled_x]);
		alpha += tmp;
		color += tmp * spu->image[base + (unsigned int) unscaled_x];
		/* 2: top center part */
		if (width > 0) {
		  unsigned int walkx;
		  for (walkx = left_right_column; walkx < (unsigned int) unscaled_x_right; ++walkx) {
		    base = spu->stride * (unsigned int) unscaled_y + walkx;
		    tmp = /* 1.0 * */ top * canon_alpha(spu->aimage[base]);
		    alpha += tmp;
		    color += tmp * spu->image[base];
		  }
		}
		/* 3: top right part */
		if (right > 0.0) {
		  base = spu->stride * (unsigned int) unscaled_y + (unsigned int) unscaled_x_right;
		  tmp = right * top * canon_alpha(spu->aimage[base]);
		  alpha += tmp;
		  color += tmp * spu->image[base];
		}
		/* 4: center left part */
		if (height > 0) {
		  unsigned int walky;
		  for (walky = top_low_row; walky < (unsigned int) unscaled_y_bottom; ++walky) {
		    base = spu->stride * walky + (unsigned int) unscaled_x;
		    tmp = left /* * 1.0 */ * canon_alpha(spu->aimage[base]);
		    alpha += tmp;
		    color += tmp * spu->image[base];
		  }
		}
		/* 5: center part */
		if (width > 0 && height > 0) {
		  unsigned int walky;
		  for (walky = top_low_row; walky < (unsigned int) unscaled_y_bottom; ++walky) {
		    unsigned int walkx;
		    base = spu->stride * walky;
		    for (walkx = left_right_column; walkx < (unsigned int) unscaled_x_right; ++walkx) {
		      tmp = /* 1.0 * 1.0 * */ canon_alpha(spu->aimage[base + walkx]);
		      alpha += tmp;
		      color += tmp * spu->image[base + walkx];
		    }
		  }
		}
		/* 6: center right part */
		if (right > 0.0 && height > 0) {
		  unsigned int walky;
		  for (walky = top_low_row; walky < (unsigned int) unscaled_y_bottom; ++walky) {
		    base = spu->stride * walky + (unsigned int) unscaled_x_right;
		    tmp = right /* * 1.0 */ * canon_alpha(spu->aimage[base]);
		    alpha += tmp;
		    color += tmp * spu->image[base];
		  }
		}
		/* 7: bottom left part */
		if (bottom > 0.0) {
		  base = spu->stride * (unsigned int) unscaled_y_bottom + (unsigned int) unscaled_x;
		  tmp = left * bottom * canon_alpha(spu->aimage[base]);
		  alpha += tmp;
		  color += tmp * spu->image[base];
		}
		/* 8: bottom center part */
		if (width > 0 && bottom > 0.0) {
		  unsigned int walkx;
		  base = spu->stride * (unsigned int) unscaled_y_bottom;
		  for (walkx = left_right_column; walkx < (unsigned int) unscaled_x_right; ++walkx) {
		    tmp = /* 1.0 * */ bottom * canon_alpha(spu->aimage[base + walkx]);
		    alpha += tmp;
		    color += tmp * spu->image[base + walkx];
		  }
		}
		/* 9: bottom right part */
		if (right > 0.0 && bottom > 0.0) {
		  base = spu->stride * (unsigned int) unscaled_y_bottom + (unsigned int) unscaled_x_right;
		  tmp = right * bottom * canon_alpha(spu->aimage[base]);
		  alpha += tmp;
		  color += tmp * spu->image[base];
		}
		/* Finally mix these transparency and brightness information suitably */
		base = spu->scaled_stride * y + x;
		spu->scaled_image[base] = alpha > 0 ? color / alpha : 0;
		spu->scaled_aimage[base] = alpha * scalex * scaley / 0x10000;
		if (spu->scaled_aimage[base]) {
		  spu->scaled_aimage[base] = 256 - spu->scaled_aimage[base];
		  if (spu->scaled_aimage[base] + spu->scaled_image[base] > 255)
		    spu->scaled_image[base] = 256 - spu->scaled_aimage[base];
		}
	      }
	    }
	  }
	  }
nothing_to_do:
	  /* Kludge: draw_alpha needs width multiple of 8. */
	  if (spu->scaled_width < spu->scaled_stride)
	    for (y = 0; y < spu->scaled_height; ++y) {
	      memset(spu->scaled_aimage + y * spu->scaled_stride + spu->scaled_width, 0,
		     spu->scaled_stride - spu->scaled_width);
	    }
	  spu->scaled_frame_width = dxs;
	  spu->scaled_frame_height = dys;
	}
      }
      if (spu->scaled_image){
	switch (spu_alignment) {
	case 0:
	  spu->scaled_start_row = dys*sub_data.pos/100;
	  if (spu->scaled_start_row + spu->scaled_height > dys)
	    spu->scaled_start_row = dys - spu->scaled_height;
	  break;
	case 1:
	  spu->scaled_start_row = dys*sub_data.pos/100 - spu->scaled_height/2;
	  if (sub_data.pos < 50) {
	    if ((int)(spu->scaled_start_row) < 0) spu->scaled_start_row = 0;
	  } else {
	    if (spu->scaled_start_row + spu->scaled_height > dys)
	      spu->scaled_start_row = dys - spu->scaled_height;
	  }
	  break;
	case 2:
	  spu->scaled_start_row = dys*sub_data.pos/100 - spu->scaled_height;
	  if ((int)(spu->scaled_start_row) < 0) spu->scaled_start_row = 0;
	  break;
	}
	draw_alpha(vo,dae_curr_vdecoded(mpxp_context().engine().xp_core),spu->scaled_start_col, spu->scaled_start_row, spu->scaled_width, spu->scaled_height,
		   spu->scaled_image, spu->scaled_aimage, spu->scaled_stride);
	spu->spu_changed = 0;
      }
    }
  } else {
    mpxp_dbg2<<"SPU not displayed: start_pts="<<spu->start_pts<<" end_pts="<<spu->end_pts<<" now_pts="<<spu->now_pts<<std::endl;
  }
}

void __FASTCALL__ spudec_update_palette(any_t* self,const unsigned int *palette)
{
  spudec_handle_t *spu = (spudec_handle_t *) self;
  if (spu && palette) {
    memcpy(spu->global_palette, palette, sizeof(spu->global_palette));
  }
}

void __FASTCALL__ spudec_set_font_factor(any_t*self, double factor)
{
  spudec_handle_t *spu = (spudec_handle_t *) self;
  spu->font_start_level = (int)(0xF0-(0xE0*factor));
}

any_t* __FASTCALL__ spudec_new_scaled(unsigned int *palette, unsigned int frame_width, unsigned int frame_height)
{
  return spudec_new_scaled_vobsub(palette, NULL, 0, frame_width, frame_height);
}

/* get palette custom color, width, height from .idx file */
any_t* __FASTCALL__ spudec_new_scaled_vobsub(unsigned int *palette, unsigned int *cuspal, unsigned int custom, unsigned int frame_width, unsigned int frame_height)
{
    spudec_handle_t *self = new(zeromem) spudec_handle_t;
    //(fprintf(stderr,"VobSub Custom Palette: %d,%d,%d,%d", self->cuspal[0], self->cuspal[1], self->cuspal[2],self->cuspal[3]);
    self->packet = NULL;
    self->image = NULL;
    self->scaled_image = NULL;
    /* XXX Although the video frame is some size, the SPU frame is
       always maximum size i.e. 720 wide and 576 or 480 high */
    self->orig_frame_width = 720;
    self->orig_frame_height = (frame_height == 480 || frame_height == 240) ? 480 : 576;
    self->custom = custom;
    // set up palette:
    self->auto_palette = 1;
    if (palette){
      memcpy(self->global_palette, palette, sizeof(self->global_palette));
      self->auto_palette = 0;
    }
    self->custom = custom;
    if (custom && cuspal) {
      memcpy(self->cuspal, cuspal, sizeof(self->cuspal));
      self->auto_palette = 0;
    }
    // forced subtitles default: show all subtitles
    self->forced_subs_only=0;
    self->is_forced_sub=0;
    return self;
}

any_t* __FASTCALL__ spudec_new(unsigned int *palette)
{
    return spudec_new_scaled(palette, 0, 0);
}

void __FASTCALL__ spudec_free(any_t*self)
{
  spudec_handle_t *spu = (spudec_handle_t*)self;
  if (spu) {
    while (spu->queue_head)
      spudec_free_packet(spudec_dequeue_packet(spu));
    if (spu->packet) delete spu->packet;
    if (spu->scaled_image) delete spu->scaled_image;
    if (spu->image) delete spu->image;
    delete spu;
  }
}
