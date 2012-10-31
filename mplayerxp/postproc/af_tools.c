#include <math.h>
#include <string.h>
#include <af.h>

/* Convert to gain value from dB. Returns CONTROL_OK if of and CONTROL_ERROR if
   fail */
ControlCodes __FASTCALL__ af_from_dB(int n, float* in, float* out, float k, float mi, float ma)
{
  int i = 0; 
  // Sanity check
  if(!in || !out) 
    return CONTROL_ERROR;

  for(i=0;i<n;i++){
    if(in[i]<=-200)
      out[i]=0.0;
    else
      out[i]=pow(10.0,clamp(in[i],mi,ma)/k);
  }
  return CONTROL_OK;
}

/* Convert from gain value to dB. Returns CONTROL_OK if of and CONTROL_ERROR if
   fail */
ControlCodes __FASTCALL__ af_to_dB(int n, float* in, float* out, float k)
{
  int i = 0; 
  // Sanity check
  if(!in || !out) 
    return CONTROL_ERROR;

  for(i=0;i<n;i++){
    if(in[i] == 0.0)
      out[i]=-200.0;
    else
      out[i]=k*log10(in[i]);
  }
  return CONTROL_OK;
}

/* Convert from ms to sample time */
ControlCodes __FASTCALL__ af_from_ms(int n, float* in, int* out, int rate, float mi, float ma)
{
  int i = 0; 
  // Sanity check
  if(!in || !out) 
    return CONTROL_ERROR;

  for(i=0;i<n;i++)
    out[i]=(int)((float)rate * clamp(in[i],mi,ma)/1000.0);

  return CONTROL_OK;
}

/* Convert from sample time to ms */
ControlCodes __FASTCALL__ af_to_ms(int n, int* in, float* out, int rate)
{
  int i = 0;
  // Sanity check
  if(!in || !out || !rate)
    return CONTROL_ERROR;

  for(i=0;i<n;i++)
    out[i]=1000.0 * (float)in[i]/((float)rate);

  return CONTROL_OK;
}

/* Helper function for testing the output format */
ControlCodes __FASTCALL__ af_test_output(struct af_instance_s* af, af_data_t* out)
{
  if((af->data->format != out->format) || 
     (af->data->bps    != out->bps)    ||
     (af->data->rate   != out->rate)   ||
     (af->data->nch    != out->nch)){
#if 0
    MSG_DBG2("af_test_out %i!=%i || %i!=%i || %i!=%i || %i!=%i\n",
    af->data->format,out->format,
    af->data->bps,out->bps,
    af->data->rate,out->rate,
    af->data->nch,out->nch);
#endif
    memcpy(out,af->data,sizeof(af_data_t));
    return CONTROL_FALSE;
  }
  return CONTROL_OK;
}
