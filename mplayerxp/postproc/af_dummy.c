/* The name speaks for itself this filter is a dummy and will not blow
   up regardless of what you do with it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "af.h"
#include "mp_config.h"
#include "osdep/mplib.h"
#include "pp_msg.h"

// Initialization and runtime control
static ControlCodes __FASTCALL__ control(struct af_instance_s* af, int cmd, any_t* arg)
{
  switch(cmd){
  case AF_CONTROL_REINIT:
    memcpy(af->data,(af_data_t*)arg,sizeof(af_data_t));
    MSG_V("[dummy] Was reinitialized, rate=%iHz, nch = %i, format = 0x%08X and bps = %i\n",af->data->rate,af->data->nch,af->data->format,af->data->bps);
    return CONTROL_OK;
  default: break;
  }
  return CONTROL_UNKNOWN;
}

// Deallocate memory 
static void __FASTCALL__ uninit(struct af_instance_s* af)
{
  if(af->data)
    mp_free(af->data);
}

// Filter data through filter
static af_data_t* __FASTCALL__ play(struct af_instance_s* af, af_data_t* data,int final)
{
  // Do something necessary to get rid of annoying warning during compile
  if(!af)
    MSG_ERR("EEEK: Argument af == NULL in af_dummy.c play().");
  return data;
}

// Allocate memory and set function pointers
static ControlCodes __FASTCALL__ open(af_instance_t* af){
  af->control=control;
  af->uninit=uninit;
  af->play=play;
  af->mul.d=1;
  af->mul.n=1;
  af->data=mp_malloc(sizeof(af_data_t));
  if(af->data == NULL)
    return CONTROL_ERROR;
  return CONTROL_OK;
}

// Description of this filter
const af_info_t af_info_dummy = {
    "dummy",
    "dummy",
    "Anders",
    "",
    AF_FLAGS_REENTRANT,
    open
};
