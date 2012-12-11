#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#ifdef MP_DEBUG
#include <assert.h>
#endif
#include "libmpstream2/stream.h"
#include "playtree.h"
#define MSGT_CLASS MSGT_PLAYTREE
#include "mp_msg.h"

static int
play_tree_is_valid(play_tree_t* pt);

play_tree_t*
play_tree_new(void) {
  play_tree_t* r = (play_tree_t*)mp_calloc(1,sizeof(play_tree_t));
  if(r == NULL)
    MSG_ERR("Can't allocate %d bytes of memory\n",sizeof(play_tree_t));
  r->entry_type = PLAY_TREE_ENTRY_NODE;
  return r;
}

void
play_tree_free(play_tree_t* pt, int childs) {
  play_tree_t* iter;

#ifdef MP_DEBUG
  assert(pt != NULL);
#endif

  if(childs) {
    for(iter = pt->child; iter != NULL; ) {
      play_tree_t* nxt=iter->next;
      play_tree_free(iter,1);
      iter = nxt;
    }
    pt->child = NULL;
  }

  play_tree_remove(pt,0,0);

  for(iter = pt->child ; iter != NULL ; iter = iter->next)
    iter->parent = NULL;

  //if(pt->params) delete pt->params;
  if(pt->files) {
    int i;
    for(i = 0 ; pt->files[i] != NULL ; i++)
      delete pt->files[i];
    delete pt->files;
  }

  delete pt;
}

void
play_tree_free_list(play_tree_t* pt, int childs) {
  play_tree_t* iter;

#ifdef MP_DEBUG
  assert(pt != NULL);
#endif

  for(iter = pt ; iter->prev != NULL ; iter = iter->prev)
    /* NOTHING */;

  while(iter) {
    play_tree_t* nxt = iter->next;
    play_tree_free(iter, childs);
    iter = nxt;
  }

}

void
play_tree_append_entry(play_tree_t* pt, play_tree_t* entry) {
  play_tree_t* iter;

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(entry != NULL);
#endif

  if(pt == entry)
    return;

  for(iter = pt ; iter->next != NULL ; iter = iter->next)
    /* NOTHING */;

  entry->parent = iter->parent;
  entry->prev = iter;
  entry->next = NULL;
  iter->next = entry;
}

void
play_tree_prepend_entry(play_tree_t* pt, play_tree_t* entry) {
  play_tree_t* iter;

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(entry != NULL);
#endif

  for(iter = pt ; iter->prev != NULL; iter = iter->prev)
    /* NOTHING */;

  entry->prev = NULL;
  entry->next = iter;
  entry->parent = iter->parent;

  iter->prev = entry;
  if(entry->parent) {
#ifdef MP_DEBUG
    assert(entry->parent->child == iter);
#endif
    entry->parent->child = entry;
  }
}

void
play_tree_insert_entry(play_tree_t* pt, play_tree_t* entry) {

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(entry != NULL);
#endif

  entry->parent = pt->parent;
  entry->prev = pt;
  if(pt->next) {
#ifdef MP_DEBUG
    assert(pt->next->prev == pt);
#endif
    entry->next = pt->next;
    entry->next->prev = entry;
  } else
    entry->next = NULL;
  pt->next = entry;

}

void
play_tree_remove(play_tree_t* pt, int free_it,int with_childs) {

#ifdef MP_DEBUG
  assert(pt != NULL);
#endif

  // Middle of list
  if(pt->prev && pt->next) {
#ifdef MP_DEBUG
    assert(pt->prev->next == pt);
    assert(pt->next->prev == pt);
#endif
    pt->prev->next = pt->next;
    pt->next->prev = pt->prev;
  } // End of list
  else if(pt->prev) {
#ifdef MP_DEBUG
    assert(pt->prev->next == pt);
#endif
    pt->prev->next = NULL;
  } // Begining of list
  else if(pt->next) {
#ifdef MP_DEBUG
    assert(pt->next->prev == pt);
#endif
    pt->next->prev = NULL;
    if(pt->parent) {
#ifdef MP_DEBUG
      assert(pt->parent->child == pt);
#endif
      pt->parent->child = pt->next;
    }
  } // The only one
  else if(pt->parent) {
#ifdef MP_DEBUG
    assert(pt->parent->child == pt);
#endif
    pt->parent->child = NULL;
  }

  pt->prev = pt->next = pt->parent = NULL;
  if(free_it)
    play_tree_free(pt,with_childs);

}

void
play_tree_set_child(play_tree_t* pt, play_tree_t* child) {
  play_tree_t* iter;

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(pt->entry_type == PLAY_TREE_ENTRY_NODE);
#endif

  for(iter = pt->child ; iter != NULL ; iter = iter->next)
    iter->parent = NULL;

  // Go back to first one
  for(iter = child ; iter->prev != NULL ; iter = iter->prev)
    /* NOTHING */;

  pt->child = iter;

  for( ; iter != NULL ; iter= iter->next)
    iter->parent = pt;

}

void
play_tree_set_parent(play_tree_t* pt, play_tree_t* parent) {
  play_tree_t* iter;

#ifdef MP_DEBUG
  assert(pt != NULL);
#endif

  if(pt->parent)
    pt->parent->child = NULL;

  for(iter = pt ; iter != NULL ; iter = iter->next)
    iter->parent = parent;

  if(pt->prev) {
    for(iter = pt->prev ; iter->prev != NULL ; iter = iter->prev)
      iter->parent = parent;
    iter->parent = parent;
    parent->child = iter;
  } else
    parent->child = pt;

}


void
play_tree_add_file(play_tree_t* pt,const char* file) {
  int n = 0;
  char* e;

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(pt->child == NULL);
  assert(file != NULL);
#endif

  if(pt->entry_type != PLAY_TREE_ENTRY_NODE &&
     pt->entry_type != PLAY_TREE_ENTRY_FILE)
    return;

  if(pt->files) {
    for(n = 0 ; pt->files[n] != NULL ; n++)
      /* NOTHING */;
  }
  pt->files = (char**)mp_realloc(pt->files,(n+2)*sizeof(char*));
  if(pt->files ==NULL) {
    MSG_ERR("Can't allocate %d bytes of memory\n",(n+2)*sizeof(char*));
    return;
  }

  e = pt->files[n] = mp_strdup(file);
  pt->files[n+1] = NULL;

  if(strncasecmp(e,"vcd://",6) == 0) {
    pt->entry_type = PLAY_TREE_ENTRY_VCD;
    memmove(e,e + 6,strlen(&e[6])+1);
  } else if(strncasecmp(e,"dvd://",6) == 0) {
    pt->entry_type = PLAY_TREE_ENTRY_DVD;
    memmove(e,&e[6],strlen(&e[6])+1);
  } else if(strncasecmp(e,"tv://",5) == 0) {
    pt->entry_type = PLAY_TREE_ENTRY_TV;
    memmove(e,&e[5],strlen(&e[5])+1);
  } else
    pt->entry_type = PLAY_TREE_ENTRY_FILE;

}

int
play_tree_remove_file(play_tree_t* pt,const char* file) {
  int n,f = -1;

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(file != NULL);
  assert(pt->entry_type != PLAY_TREE_ENTRY_NODE);
#endif

  for(n=0 ; pt->files[n] != NULL ; n++) {
    if(strcmp(file,pt->files[n]) == 0)
      f = n;
  }

  if(f < 0) // Not found
    return 0;

#ifdef MP_DEBUG
  assert(n > f);
#endif

  delete pt->files[f];

  if(n > 1) {
    memmove(&pt->files[f],&pt->files[f+1],(n-f)*sizeof(char*));
    pt->files = (char**)mp_realloc(pt->files,n*sizeof(char*));
    if(pt->files == NULL) {
      MSG_ERR("Can't allocate %d bytes of memory\n",(n+2)*sizeof(char*));
      return -1;
    }
  } else {
    delete pt->files;
    pt->files = NULL;
  }

  return 1;
}

void play_tree_set_param(play_tree_t* pt,const char* name,const char* val) {
  int n = 0,ni = -1;

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(name != NULL);
#endif

  if(pt->params) {
    for( ; pt->params[n].name != NULL ; n++) {
      if(strcasecmp(pt->params[n].name,name) == 0)
	ni = n;
    }
  }

  if(ni > 0) {
    if(pt->params[n].value != NULL) delete pt->params[n].value;
    pt->params[n].value = val != NULL ? mp_strdup(val) : NULL;
    return;
  }

  pt->params = (play_tree_param_t*)mp_realloc(pt->params,(n+2)*sizeof(play_tree_param_t));
  if(pt->params == NULL)
  {
    MSG_FATAL("Can't mp_realloc params\n");
    return;
  }
  pt->params[n].name = mp_strdup(name);
  pt->params[n].value = val != NULL ? mp_strdup(val) : NULL;
  memset(&pt->params[n+1],0,sizeof(play_tree_param_t));

  return;
}

int play_tree_unset_param(play_tree_t* pt,const char* name) {
  int n,ni = -1;

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(name != NULL);
  assert(pt->params != NULL);
#endif

  for(n = 0 ; pt->params[n].name != NULL ; n++) {
    if(strcasecmp(pt->params[n].name,name) == 0)
      ni = n;
  }

  if(ni < 0)
    return 0;

  if(pt->params[ni].name) delete pt->params[ni].name;
  if(pt->params[ni].value) delete pt->params[ni].value;

  if(n > 1) {
    memmove(&pt->params[ni],&pt->params[ni+1],(n-ni)*sizeof(play_tree_param_t));
    pt->params = (play_tree_param_t*)mp_realloc(pt->params,n*sizeof(play_tree_param_t));
    if(pt->params == NULL) {
      MSG_ERR("Can't allocate %d bytes of memory\n",n*sizeof(play_tree_param_t));
      return -1;
    }
  } else {
    delete pt->params;
    pt->params = NULL;
  }

  return 1;
}

void play_tree_set_params_from(play_tree_t* dest,const play_tree_t* src) {
  int i;

#ifdef MP_DEBUG
  assert(dest != NULL);
  assert(src != NULL);
#endif

  if(!src->params)
    return;

  for(i = 0; src->params[i].name != NULL ; i++)
    play_tree_set_param(dest,src->params[i].name,src->params[i].value);
  if(src->flags & PLAY_TREE_RND) // pass the random flag too
    dest->flags |= PLAY_TREE_RND;

}

// all children if deep < 0
void play_tree_set_flag(play_tree_t* pt, int flags , int deep) {
  play_tree_t*  i;

  pt->flags |= flags;

  if(deep && pt->child) {
    if(deep > 0) deep--;
    for(i = pt->child ; i ; i = i->next)
      play_tree_set_flag(i,flags,deep);
  }
}

void play_tree_unset_flag(play_tree_t* pt, int flags , int deep) {
  play_tree_t*  i;

  pt->flags &= ~flags;

  if(deep && pt->child) {
    if(deep > 0) deep--;
    for(i = pt->child ; i ; i = i->next)
      play_tree_unset_flag(i,flags,deep);
  }
}

static void
play_tree_iter_push_params(play_tree_iter_t* iter) {
  int n;
  play_tree_t* pt;
#ifdef MP_DEBUG
  assert(iter != NULL);
  assert(iter->config != NULL);
  assert(iter->tree != NULL);
#endif

  pt = iter->tree;

  // We always push a config because we can set some option
  // while playing
  m_config_push(iter->config);

  if(pt->params == NULL)
    return;


  for(n = 0; pt->params[n].name != NULL ; n++) {
    int e;
    if((e = m_config_set_option(iter->config,pt->params[n].name,pt->params[n].value)) < 0) {
      MSG_ERR("Error %d while setting option '%s' with value '%s'\n",e,
	     pt->params[n].name,pt->params[n].value);
    }
  }

  if(!pt->child)
    iter->entry_pushed = 1;
  return;
}

play_tree_iter_t*
play_tree_iter_new(play_tree_t* pt,m_config_t* config) {
  play_tree_iter_t* iter;

#ifdef MP_DEBUG
  assert(pt != NULL);
  assert(config != NULL);
#endif

  if( ! play_tree_is_valid(pt))
    return NULL;

  iter = (play_tree_iter_t*)mp_calloc(1,sizeof(play_tree_iter_t));
  if(! iter) return NULL;
  iter->root = pt;
  iter->tree = NULL;
  iter->config = config;

  if(pt->parent)
    iter->loop = pt->parent->loop;

  return iter;
}

void
play_tree_iter_free(play_tree_iter_t* iter) {

#ifdef MP_DEBUG
  assert(iter != NULL);
#endif

  if(iter->status_stack) {
#ifdef MP_DEBUG
    assert(iter->stack_size > 0);
#endif
    delete iter->status_stack;
  }

  delete iter;
}

static play_tree_t*
play_tree_rnd_step(play_tree_t* pt) {
  int count = 0;
  int r,rnd;
  time_t tim;
  play_tree_t *i,*head;

  // Count how many mp_free choice we have
  for(i = pt ; i->prev ; i = i->prev)
    if(!(i->flags & PLAY_TREE_RND_PLAYED)) count++;
  head = i;
  if(!(i->flags & PLAY_TREE_RND_PLAYED)) count++;
  if(pt->next)
  for(i = pt->next ; i ; i = i->next)
    if(!(i->flags & PLAY_TREE_RND_PLAYED)) count++;

  if(!count) return NULL;
  /* make it time depended */
  time(&tim);
  /* integer between 0 and RAND_MAX inclusive. */
  rnd=rand();
  r = (int)(((float)(count) * rnd) / (RAND_MAX + 1.0));
  if(r) rnd = r = count - (tim%r);

  for(i = head ; i  ; i=i->next) {
    if(!(i->flags & PLAY_TREE_RND_PLAYED)) r--;
    if(r < 0) return i;
  }
  for(i = head ; i  ; i=i->next) {
    if(!(i->flags & PLAY_TREE_RND_PLAYED)) return i;
  }

  MSG_ERR("Random stepping error r=%i\n",rnd);
  return NULL;
}

int
play_tree_iter_step(play_tree_iter_t* iter, int d,int with_nodes) {
  play_tree_t* pt;
  int rnd;
#ifdef MP_DEBUG
  assert(iter != NULL);
  assert(iter->root != NULL);
#endif

  if(iter->tree == NULL) {
    iter->tree = iter->root;
    return play_tree_iter_step(iter,0,with_nodes);
  }

  if(iter->config && iter->entry_pushed > 0) {
    iter->entry_pushed = 0;
    m_config_pop(iter->config);
  }

  if(iter->tree->parent && (iter->tree->parent->flags & PLAY_TREE_RND))
    rnd=iter->mode = PLAY_TREE_ITER_RND;
  else
    rnd=iter->mode = PLAY_TREE_ITER_NORMAL;

  iter->file = -1;
  if(iter->mode == PLAY_TREE_ITER_RND)
    pt = play_tree_rnd_step(iter->tree);
  else if( d > 0 ) {
    int i;
    pt = iter->tree;
    for(i = d ; i > 0 && pt ; i--)
      pt = pt->next;
    d = i ? i : 1;
  } else if(d < 0) {
    int i;
    pt = iter->tree;
    for(i = d ; i < 0 && pt ; i++)
      pt = pt->prev;
    d = i ? i : -1;
  } else
    pt = iter->tree;

  if(pt == NULL) { // No next
    // Must we loop?
    if (iter->mode == PLAY_TREE_ITER_RND) {
      if (iter->root->loop == 0)
	return PLAY_TREE_ITER_END;
      play_tree_unset_flag(iter->root, PLAY_TREE_RND_PLAYED, -1);
      if (iter->root->loop > 0) iter->root->loop--;
      // try again
      return play_tree_iter_step(iter, 0, with_nodes);
    } else
    if(iter->tree->parent && iter->tree->parent->loop != 0 && ((d > 0 && iter->loop != 0) || ( d < 0 && (iter->loop < 0 || iter->loop < iter->tree->parent->loop) ) ) ) {
      if(d > 0) { // Go back to the first one
	for(pt = iter->tree ; pt->prev != NULL; pt = pt->prev)
	  /* NOTHNG */;
	if(iter->loop > 0) iter->loop--;
      } else if( d < 0 ) { // Or the last one
	for(pt = iter->tree ; pt->next != NULL; pt = pt->next)
	  /* NOTHNG */;
	if(iter->loop >= 0 && iter->loop < iter->tree->parent->loop) iter->loop++;
      }
      iter->tree = pt;
      return play_tree_iter_step(iter,0,with_nodes);
    }
    // Go up one level
    return play_tree_iter_up_step(iter,d,with_nodes);

  }

  // Is there any valid child?
  if(pt->child && play_tree_is_valid(pt->child)) {
    iter->tree = pt;
    if(with_nodes) { // Stop on the node
      return PLAY_TREE_ITER_NODE;
    } else      // Or follow it
      return play_tree_iter_down_step(iter,d,with_nodes);
  }

  // Is it a valid entry?
  if(! play_tree_is_valid(pt)) {
    if(d == 0 && rnd==PLAY_TREE_ITER_NORMAL) { // Can this happen ? FF: Yes!
      MSG_ERR("What to do now ???? Infinite loop if we continue\n");
      return PLAY_TREE_ITER_ERROR;
    } // Not a valid entry : go to next one
    return play_tree_iter_step(iter,d,with_nodes);
  }

#ifdef MP_DEBUG
  assert(pt->files != NULL);
#endif

  iter->tree = pt;

  for(d = 0 ; iter->tree->files[d] != NULL ; d++)
    /* NOTHING */;
  iter->num_files = d;

  if(iter->config) {
    play_tree_iter_push_params(iter);
    iter->entry_pushed = 1;
    if(iter->mode == PLAY_TREE_ITER_RND)
      pt->flags |= PLAY_TREE_RND_PLAYED;
  }

  return PLAY_TREE_ITER_ENTRY;

}

static int
play_tree_is_valid(play_tree_t* pt) {
  play_tree_t* iter;

#ifdef MP_DEBUG
  assert(pt != NULL);
#endif

  if(pt->entry_type != PLAY_TREE_ENTRY_NODE) {
#ifdef MP_DEBUG
    assert(pt->child == NULL);
#endif
    return 1;
  }
  else if (pt->child != NULL) {
    for(iter = pt->child ; iter != NULL ; iter = iter->next) {
      if(play_tree_is_valid(iter))
	return 1;
    }
  }
  return 0;
}

int
play_tree_iter_up_step(play_tree_iter_t* iter, int d,int with_nodes) {

#ifdef MP_DEBUG
  assert(iter != NULL);
  assert(iter->tree != NULL);
#endif

  iter->file = -1;
  if(iter->tree->parent == iter->root->parent)
    return PLAY_TREE_ITER_END;

#ifdef MP_DEBUG
  assert(iter->tree->parent != NULL);
  assert(iter->stack_size > 0);
  assert(iter->status_stack != NULL);
#endif

  iter->stack_size--;
  iter->loop = iter->status_stack[iter->stack_size];
  if(iter->stack_size > 0)
    iter->status_stack = (int*)mp_realloc(iter->status_stack,iter->stack_size*sizeof(int));
  else {
    delete iter->status_stack;
    iter->status_stack = NULL;
  }
  if(iter->stack_size > 0 && iter->status_stack == NULL) {
    MSG_ERR("Can't allocate %d bytes of memory\n",iter->stack_size*sizeof(char*));
    return PLAY_TREE_ITER_ERROR;
  }
  iter->tree = iter->tree->parent;

  // Pop subtree params
  if(iter->config) {
    m_config_pop(iter->config);
    if(iter->mode == PLAY_TREE_ITER_RND)
      iter->tree->flags |= PLAY_TREE_RND_PLAYED;
  }

  return play_tree_iter_step(iter,d,with_nodes);
}

int
play_tree_iter_down_step(play_tree_iter_t* iter, int d,int with_nodes) {

#ifdef MP_DEBUG
  assert(iter->tree->files == NULL);
  assert(iter->tree->child != NULL);
  assert(iter->tree->child->parent == iter->tree);
#endif

  iter->file = -1;

  //  Push subtree params
  if(iter->config)
    play_tree_iter_push_params(iter);

  iter->stack_size++;
  iter->status_stack = (int*)mp_realloc(iter->status_stack,iter->stack_size*sizeof(int));
  if(iter->status_stack == NULL) {
    MSG_ERR("Can't allocate %d bytes of memory\n",iter->stack_size*sizeof(int));
    return PLAY_TREE_ITER_ERROR;
  }
  iter->status_stack[iter->stack_size-1] = iter->loop;
  // Set new status
  iter->loop = iter->tree->loop-1;
  if(d >= 0)
    iter->tree = iter->tree->child;
  else {
    play_tree_t* pt;
    for(pt = iter->tree->child ; pt->next != NULL ; pt = pt->next)
      /*NOTING*/;
    iter->tree = pt;
  }

  return play_tree_iter_step(iter,0,with_nodes);
}

// This is used as a file name for vcd/tv/dvd
char playtree_ret_filename[256];

char*
play_tree_iter_get_file(play_tree_iter_t* iter, int d) {
  char* entry;
#ifdef MP_DEBUG
  assert(iter != NULL);
  assert(iter->tree->child == NULL);
#endif

  if(iter->tree->files == NULL)
    return NULL;

#ifdef MP_DEBUG
  assert(iter->num_files > 0);
#endif

  if(iter->file >= iter->num_files-1 || iter->file < -1)
    return NULL;

  if(d > 0) {
    if(iter->file >= iter->num_files - 1)
      iter->file = 0;
    else
      iter->file++;
  } else if(d < 0) {
    if(iter->file <= 0)
      iter->file = iter->num_files - 1;
    else
      iter->file--;
  }
  entry = iter->tree->files[iter->file];

  switch(iter->tree->entry_type) {
  case PLAY_TREE_ENTRY_DVD :
    if(strlen(entry) == 0) entry = "1";
    if(iter->config)
      m_config_set_option(iter->config,"dvd",entry);
    snprintf(playtree_ret_filename,255,"DVD title %s",entry);
    return playtree_ret_filename;
  case PLAY_TREE_ENTRY_VCD :
    if(strlen(entry) == 0) entry = "1";
    if(iter->config)
      m_config_set_option(iter->config,"vcd",entry);
    snprintf(playtree_ret_filename,255,"vcd://%s",entry);
    return playtree_ret_filename;
  case PLAY_TREE_ENTRY_TV :
    {
      if(strlen(entry) != 0) {
	char *s,*e, *val = (char*)mp_malloc(strlen(entry) + 11 + 1);
	sprintf(val,"on:channel=%s",entry);
	if(iter->config)
	  m_config_set_option(iter->config,"tv",val);
	s = playtree_ret_filename + sprintf(playtree_ret_filename,"TV channel ");
	e = strchr(entry,':');
	if(!e) strncpy(s,entry,255-11);
	else {
	  if(entry-e > 255) e = entry+255;
	  strncpy(s,entry,val-e);
	  s[val-e] = '\0';
	}
	delete val;
	return playtree_ret_filename;
      } else {
	if(iter->config)
	  m_config_set_option(iter->config,"tv","on");
	return "TV";
      }
    }
  }

  return entry;
}

play_tree_t*
play_tree_cleanup(play_tree_t* pt) {
  play_tree_t* iter, *tmp, *first;

#ifdef MP_DEBUG
  assert(pt != NULL);
#endif

  if( ! play_tree_is_valid(pt)) {
    play_tree_remove(pt,1,1);
    return NULL;
  }

  first = pt->child;

  for(iter = pt->child ; iter != NULL ; ) {
    tmp = iter;
    iter = iter->next;
    if(! play_tree_is_valid(tmp)) {
      play_tree_remove(tmp,1,1);
      if(tmp == first) first = iter;
    }
  }

  for(iter = first ; iter != NULL ; ) {
    tmp = iter;
    iter = iter->next;
    play_tree_cleanup(tmp);
  }

  return pt;

}

play_tree_iter_t* play_tree_iter_new_copy(play_tree_iter_t const* old) {
  play_tree_iter_t* iter;

#ifdef MP_DEBUG
  assert(old != NULL);
#endif

  iter = (play_tree_iter_t*)mp_malloc(sizeof(play_tree_iter_t));
  if(iter == NULL) {
    MSG_ERR("Can't allocate %d bytes of memory\n",sizeof(play_tree_iter_t));
    return NULL;
  }

  memcpy(iter,old,sizeof(play_tree_iter_t));
  if(old->status_stack) {
    iter->status_stack = (int*)mp_malloc(old->stack_size * sizeof(int));
    if(iter->status_stack == NULL) {
      MSG_ERR("Can't allocate %d bytes of memory\n",old->stack_size * sizeof(int));
      delete iter;
      return NULL;
    }
    memcpy(iter->status_stack,old->status_stack,iter->stack_size*sizeof(int));
  }
  iter->config = NULL;

  return iter;
}

// HIGH Level API, by Fabian Franz (mplayer@fabian-franz.de)
//
play_tree_iter_t* pt_iter_create(play_tree_t** ppt, m_config_t* config)
{
  play_tree_iter_t* r=NULL;
#ifdef MP_DEBUG
  assert(*ppt!=NULL);
#endif

  *ppt=play_tree_cleanup(*ppt);

  if(*ppt) {
    r = play_tree_iter_new(*ppt,config);
    if (r && play_tree_iter_step(r,0,0) != PLAY_TREE_ITER_ENTRY)
    {
      play_tree_iter_free(r);
      r = NULL;
    }
  }

  return r;
}

void pt_iter_destroy(play_tree_iter_t** iter)
{
  if (iter && *iter)
  {
    delete *iter;
    iter=NULL;
  }
}

char* pt_iter_get_file(play_tree_iter_t* iter, int d)
{
  int i=0;
  char* r;

  if (iter==NULL)
    return NULL;

  r = play_tree_iter_get_file(iter,d);

  while (!r && d!=0)
  {
    if (play_tree_iter_step(iter,d,0) != PLAY_TREE_ITER_ENTRY)
	break;
    r=play_tree_iter_get_file(iter,d);
    i++;
  }

  return r;
}

void pt_iter_insert_entry(play_tree_iter_t* iter, play_tree_t* entry)
{
  play_tree_t *pt = iter->tree;
#ifdef MP_DEBUG
  assert(pt!=NULL);
  assert(entry!=NULL);
  assert(entry!=pt);
#endif

  play_tree_insert_entry(pt, entry);
  play_tree_set_params_from(entry,pt);
}

void pt_iter_replace_entry(play_tree_iter_t* iter, play_tree_t* entry)
{
  play_tree_t *pt = iter->tree;

  pt_iter_insert_entry(iter, entry);
  play_tree_remove(pt, 1, 1);
  iter->tree=entry;
}

//Add a new file as a new entry
void pt_add_file(play_tree_t** ppt,const char* filename)
{
  play_tree_t *pt = *ppt, *entry = play_tree_new();
#ifdef MP_DEBUG
  assert(entry!=NULL);
#endif

  play_tree_add_file(entry, filename);
  if (pt)
    play_tree_append_entry(pt, entry);
  else
  {
    pt=entry;
    *ppt=pt;
  }
  play_tree_set_params_from(entry,pt);
}

void pt_add_gui_file(play_tree_t** ppt,const char* path,const char* file)
{
  char* wholename = new char [strlen(path)+strlen(file)+2];

  if (wholename)
  {
    strcpy(wholename, path);
    strcat(wholename, "/");
    strcat(wholename, file);
    pt_add_file(ppt, wholename);
    delete wholename; // As pt_add_file strdups it anyway!
  }
}

void pt_iter_goto_head(play_tree_iter_t* iter)
{
  iter->tree=iter->root;
  play_tree_iter_step(iter, 0, 0);
}
