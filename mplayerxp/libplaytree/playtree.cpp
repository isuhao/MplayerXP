#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
#include <algorithm>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "libmpstream2/stream.h"
#include "libmpconf/cfgparser.h"
#include "playtree.h"
#include "playtree_msg.h"

namespace mpxp {

PlayTree::PlayTree()
	:entry_type(PLAY_TREE_ENTRY_NODE) {}
PlayTree::~PlayTree() {}

void PlayTree::free(int childs) {
    PlayTree* iter;

    if(childs) {
	for(iter = child; iter != NULL; ) {
	    PlayTree* nxt=iter->next;
	    iter->free(1);
	    delete iter;
	    iter = nxt;
	}
	child = NULL;
    }
    remove(0,0);
    for(iter = child ; iter != NULL ; iter = iter->next) iter->parent = NULL;
}

void PlayTree::free_list(int childs) {
    PlayTree* iter;

    for(iter = this ; iter->prev != NULL ; iter = iter->prev) /* NOTHING */;
    while(iter) {
	PlayTree* nxt = iter->next;
	iter->free(childs);
	delete iter;
	iter = nxt;
    }
}

MPXP_Rc PlayTree::is_valid() const {
    PlayTree* iter;

    if(entry_type != PLAY_TREE_ENTRY_NODE) return MPXP_Ok;
    else if (child != NULL) {
	for(iter = child ; iter != NULL ; iter = iter->next) {
	    if(iter->is_valid()) return MPXP_Ok;
	}
    }
    return MPXP_False;
}

void PlayTree::append_entry(PlayTree* entry) {
    PlayTree* iter;

    if(this == entry) return;

    for(iter = this; iter->next != NULL ; iter = iter->next) /* NOTHING */;

    entry->parent = iter->parent;
    entry->prev = iter;
    entry->next = NULL;
    iter->next = entry;
}

void PlayTree::prepend_entry(PlayTree* entry) {
    PlayTree* iter;

    for(iter = this ; iter->prev != NULL; iter = iter->prev) /* NOTHING */;

    entry->prev = NULL;
    entry->next = iter;
    entry->parent = iter->parent;

    iter->prev = entry;
    if(entry->parent) entry->parent->child = entry;
}

void PlayTree::insert_entry(PlayTree* entry) {
    entry->parent = parent;
    entry->prev = this;
    if(next) {
	entry->next = next;
	entry->next->prev = entry;
    } else  entry->next = NULL;
    next = entry;
}

void PlayTree::remove(int free_it,int with_childs) {
    // Middle of list
    if(prev && next) {
	prev->next = next;
	next->prev = prev;
    } // End of list
    else if(prev) {
	prev->next = NULL;
    } // Begining of list
    else if(next) {
	next->prev = NULL;
	if(parent) parent->child = next;
    } // The only one
    else if(parent) {
	parent->child = NULL;
    }
    prev = next = parent = NULL;
    if(free_it) free(with_childs);
}

void PlayTree::set_child(PlayTree* _child) {
    PlayTree* iter;

    for(iter = _child ; iter != NULL ; iter = iter->next) iter->parent = NULL;
    // Go back to first one
    for(iter = _child ; iter->prev != NULL ; iter = iter->prev) /* NOTHING */;
    child = iter;
    for( ; iter != NULL ; iter= iter->next)  iter->parent = this;
}

void PlayTree::set_parent(PlayTree* _parent) {
    PlayTree* iter;

    if(_parent) _parent->child = NULL;
    for(iter = this ; iter != NULL ; iter = iter->next) iter->parent = _parent;
    if(prev) {
	for(iter = prev ; iter->prev != NULL ; iter = iter->prev) iter->parent = _parent;
	iter->parent=_parent;
	_parent->child=iter;
    } else _parent->child = this;
}

void PlayTree::add_file(const std::string& file) {
    if(entry_type != PLAY_TREE_ENTRY_NODE &&
	entry_type != PLAY_TREE_ENTRY_FILE)
	return;

    size_t pos;
    std::string tail,lf=file;
    std::transform(lf.begin(),lf.end(),lf.begin(), ::tolower);

    if(lf.substr(0,6)=="vcd://") {
	entry_type = PLAY_TREE_ENTRY_VCD;
	pos=6;
    } else if(lf.substr(0,6)=="dvd://") {
	entry_type = PLAY_TREE_ENTRY_DVD;
	pos=6;
    } else if(lf.substr(0,5)=="tv://") {
	entry_type = PLAY_TREE_ENTRY_TV;
	pos=5;
    } else {
	entry_type = PLAY_TREE_ENTRY_FILE;
	pos=0;
    }
    tail=file.substr(pos);
    files.push_back(tail);
}

MPXP_Rc PlayTree::remove_file(const std::string& file) {
    int f = -1;
    size_t n,sz=files.size();

    for(n=0; n<sz; n++) if(file==files[n]) f = n;

    if(f < 0) return MPXP_False; // Not found

    files.erase(files.begin()+f);

    return MPXP_Ok;
}

void PlayTree::set_param(const std::string& name,const std::string& val) {
    int ni = -1;
    size_t n,sz=params.size();

    std::string lname=name;
    std::transform(lname.begin(),lname.end(),lname.begin(), ::tolower);
    if(!params.empty()) {
	for(n=0; n<sz; n++) {
	    std::string lparm=params[n].name;
	    std::transform(lparm.begin(),lparm.end(),lparm.begin(), ::tolower);
	    if(lname==lparm) ni = n;
	}
    }

    if(ni > 0) {
	params[ni].value = val;
	return;
    }

    play_tree_param_t param;
    param.name=name;
    param.value=val;
    params.push_back(param);
}

MPXP_Rc PlayTree::unset_param(const std::string& name) {
    int ni = -1;
    size_t n,sz=params.size();

    std::string lname=name;
    std::transform(lname.begin(),lname.end(),lname.begin(), ::tolower);
    for(n=0;n<sz;n++) {
	std::string lparm=params[n].name;
	std::transform(lparm.begin(),lparm.end(),lparm.begin(), ::tolower);
	if(lname==lparm) ni = n;
    }

    if(ni < 0) return MPXP_False;

    if(n > 1)	params.erase(params.begin()+ni);
    else	params.clear();

    return MPXP_Ok;
}

void PlayTree::set_params_from(const PlayTree& src) {
    size_t i,sz=src.params.size();

    if(src.params.empty()) return;

    for(i=0;i<sz; i++)
	set_param(src.params[i].name,src.params[i].value);
    if(src.flags & PLAY_TREE_RND) // pass the random flag too
	flags |= PLAY_TREE_RND;
}

// all children if deep < 0
void PlayTree::set_flag(int _flags, int deep) {
    PlayTree* i;

    flags |= _flags;

    if(deep && child) {
	if(deep > 0) deep--;
	for(i = child ; i ; i = i->get_next()) i->set_flag(_flags,deep);
    }
}

void PlayTree::unset_flag(int _flags, int deep) {
    PlayTree* i;

    flags &= ~_flags;

    if(deep && child) {
	if(deep > 0) deep--;
	for(i = child ; i ; i = i->get_next()) i->unset_flag(_flags,deep);
    }
}

MPXP_Rc PlayTree::cleanup() {
    PlayTree* iter, *tmp, *first;

    if( ! is_valid()) {
	remove(1,1);
	return MPXP_False;
    }

    first = child;

    for(iter = child ; iter != NULL ; ) {
	tmp = iter;
	iter = iter->next;
	if(! tmp->is_valid()) {
	    tmp->remove(1,1);
	    if(tmp == first) first = iter;
	}
    }

    for(iter = first ; iter != NULL ; ) {
	tmp = iter;
	iter = iter->next;
	tmp->cleanup();
    }
    return MPXP_Ok;
}

static PlayTree*
play_tree_rnd_step(PlayTree* pt) {
  int count = 0;
  int r,rnd;
  time_t tim;
  PlayTree *i,*head;

  // Count how many mp_free choice we have
  for(i = pt ; i->get_prev() ; i = i->get_prev())
    if(!(i->get_flags() & PLAY_TREE_RND_PLAYED)) count++;
  head = i;
  if(!(i->get_flags() & PLAY_TREE_RND_PLAYED)) count++;
  if(pt->get_next())
  for(i = pt->get_next() ; i ; i = i->get_next())
    if(!(i->get_flags() & PLAY_TREE_RND_PLAYED)) count++;

  if(!count) return NULL;
  /* make it time depended */
  time(&tim);
  /* integer between 0 and RAND_MAX inclusive. */
  rnd=rand();
  r = (int)(((float)(count) * rnd) / (RAND_MAX + 1.0));
  if(r) rnd = r = count - (tim%r);

  for(i = head ; i  ; i=i->get_next()) {
    if(!(i->get_flags() & PLAY_TREE_RND_PLAYED)) r--;
    if(r < 0) return i;
  }
  for(i = head ; i ; i=i->get_next()) {
    if(!(i->get_flags() & PLAY_TREE_RND_PLAYED)) return i;
  }

  mpxp_err<<"Random stepping error r="<<rnd<<std::endl;
  return NULL;
}

_PlayTree_Iter::_PlayTree_Iter(PlayTree* pt,m_config_t& _config)
		:root(pt),
		tree(NULL),
		config(_config) {
    if(pt->get_parent()) loop = pt->get_parent()->get_loop();
}

_PlayTree_Iter::_PlayTree_Iter(const _PlayTree_Iter& old)
		:root(old.root),
		tree(old.tree),
		config(old.config),
		loop(old.loop),
		file(old.file),
		num_files(old.num_files),
		mode(old.mode) {}

_PlayTree_Iter::~_PlayTree_Iter() {}

void _PlayTree_Iter::push_params() {
    PlayTree* pt;

    pt = tree;

    // We always push a config because we can set some option
    // while playing
    m_config_push(config);

    if(pt->get_params().empty()) return;
    size_t n,sz=pt->get_params().size();

    for(n=0; n<sz; n++) {
	int e;
	if((e = m_config_set_option(config,pt->get_param(n).name,pt->get_param(n).value)) < 0)
	    mpxp_err<<"Error "<<e<<" while setting option '"<<pt->get_param(n).name<<"' with value '"<<pt->get_param(n).value<<"'"<<std::endl;
    }

    if(!pt->get_child()) entry_pushed = 1;
    return;
}

int _PlayTree_Iter::step(int d,int with_nodes) {
    PlayTree* pt;
    int rnd;

    if(tree == NULL) {
	tree = root;
	return step(0,with_nodes);
    }

    if(entry_pushed > 0) {
	entry_pushed = 0;
	m_config_pop(config);
    }

    if(tree->get_parent() && (tree->get_parent()->get_flags() & PLAY_TREE_RND))
	rnd=mode = PLAY_TREE_ITER_RND;
    else
	rnd=mode = PLAY_TREE_ITER_NORMAL;

    file = -1;
    if(mode == PLAY_TREE_ITER_RND) pt = play_tree_rnd_step(tree);
    else if( d > 0 ) {
	int i;
	pt = tree;
	for(i = d ; i > 0 && pt ; i--) pt = pt->get_next();
	d = i ? i : 1;
    } else if(d < 0) {
	int i;
	pt = tree;
	for(i = d ; i < 0 && pt ; i++)  pt = pt->get_prev();
	d = i ? i : -1;
    } else pt = tree;

    if(pt == NULL) { // No next
	// Must we loop?
	if (mode == PLAY_TREE_ITER_RND) {
	    if (root->get_loop() == 0) return PLAY_TREE_ITER_END;
	    root->unset_flag(PLAY_TREE_RND_PLAYED, -1);
	    if (root->get_loop() > 0) root->set_loop(root->get_loop()-1);
	    // try again
	    return step(0, with_nodes);
	} else if(tree->get_parent() && tree->get_parent()->get_loop() != 0 && ((d > 0 && loop != 0) || ( d < 0 && (loop < 0 || loop < tree->get_parent()->get_loop())))) {
	    if(d > 0) { // Go back to the first one
		for(pt = tree ; pt->get_prev() != NULL; pt = pt->get_prev()) /* NOTHNG */;
		if(loop > 0) loop--;
	    } else if( d < 0 ) { // Or the last one
		for(pt = tree ; pt->get_next() != NULL; pt = pt->get_next()) /* NOTHNG */;
		if(loop >= 0 && loop < tree->get_parent()->get_loop()) loop++;
	    }
	    tree = pt;
	    return step(0,with_nodes);
	}
	// Go up one level
	return up_step(d,with_nodes);
    }
    // Is there any valid child?
    if(pt->get_child() && pt->get_child()->is_valid()) {
	tree = pt;
	if(with_nodes) { // Stop on the node
	    return PLAY_TREE_ITER_NODE;
	} else      // Or follow it
	    return down_step(d,with_nodes);
    }
    // Is it a valid entry?
    if(! pt->is_valid()) {
	if(d == 0 && rnd==PLAY_TREE_ITER_NORMAL) { // Can this happen ? FF: Yes!
	    mpxp_err<<"What to do now ???? Infinite loop if we continue"<<std::endl;
	    return PLAY_TREE_ITER_ERROR;
	} // Not a valid entry : go to next one
	return step(d,with_nodes);
    }

    tree = pt;

    num_files = tree->get_files().size();

    push_params();
    entry_pushed = 1;
    if(mode == PLAY_TREE_ITER_RND) pt->set_flags(pt->get_flags()|PLAY_TREE_RND_PLAYED);

    return PLAY_TREE_ITER_ENTRY;
}

int _PlayTree_Iter::up_step(int d,int with_nodes) {
    file = -1;
    if(tree->get_parent() == root->get_parent()) return PLAY_TREE_ITER_END;

    loop = status_stack.top(); status_stack.pop();
    tree = tree->get_parent();

    // Pop subtree params
    m_config_pop(config);
    if(mode == PLAY_TREE_ITER_RND) tree->set_flags(tree->get_flags()|PLAY_TREE_RND_PLAYED);
    return step(d,with_nodes);
}

int _PlayTree_Iter::down_step(int d,int with_nodes) {
    file = -1;

    //  Push subtree params
    push_params();

    status_stack.push(loop);
    // Set new status
    loop = tree->get_loop()-1;
    if(d >= 0) tree = tree->get_child();
    else {
	PlayTree* pt;
	for(pt = tree->get_child() ; pt->get_next() != NULL ; pt = pt->get_next()) /*NOTING*/;
	tree = pt;
    }
    return step(0,with_nodes);
}

std::string _PlayTree_Iter::get_file(int d) {
    std::string entry;

    if(tree->get_files().empty()) return "";
    if(file >= num_files-1 || file < -1) return "";
    if(d > 0) {
	if(file >= num_files - 1) file = 0;
	else file++;
    } else if(d < 0) {
	if(file <= 0) file = num_files - 1;
	else file--;
    }
    entry = tree->get_file(file);

    switch(tree->get_entry_type()) {
	case PLAY_TREE_ENTRY_DVD :
	    if(entry.length() == 0) entry = "1";
	    m_config_set_option(config,"dvd",entry);
	    return std::string("DVD title ")+entry;
	case PLAY_TREE_ENTRY_VCD :
	    if(entry.length() == 0) entry = "1";
	    m_config_set_option(config,"vcd",entry);
	    return std::string("vcd://")+entry;
	case PLAY_TREE_ENTRY_TV: {
	    if(entry.length() != 0) {
		std::string val;
		size_t e;
		std::string rs;
		val="on:channel="+entry;
		m_config_set_option(config,"tv",val);
		rs="TV channel ";
		e = entry.find(':');
		if(e==std::string::npos) rs+=entry.substr(0,255-11);
		else {
		    if(e > 255) e = 255;
		    rs+=entry.substr(0,e);
		}
		return rs;
	    } else m_config_set_option(config,"tv","on");
	    return "TV";
	}
        default: break;
    }
    return entry;
}

} // namespace mpxp

void pt_iter_destroy(_PlayTree_Iter** iter)
{
  if (iter && *iter)
  {
    delete *iter;
    iter=NULL;
  }
}

std::string pt_iter_get_file(_PlayTree_Iter* iter, int d)
{
  int i=0;
  std::string r;

  if (iter==NULL)
    return NULL;

  r = iter->get_file(d);

  while (r.empty() && d!=0)
  {
    if (iter->step(d,0) != PLAY_TREE_ITER_ENTRY) break;
    r=iter->get_file(d);
    i++;
  }

  return r;
}

void pt_iter_insert_entry(_PlayTree_Iter* iter, PlayTree* entry)
{
  PlayTree *pt = iter->get_tree();

  pt->insert_entry(entry);
  entry->set_params_from(*pt);
}

void pt_iter_replace_entry(_PlayTree_Iter* iter, PlayTree* entry)
{
  PlayTree *pt = iter->get_tree();

  pt_iter_insert_entry(iter, entry);
  pt->remove(1, 1);
  iter->set_tree(entry);
}

//Add a new file as a new entry
void pt_add_file(PlayTree** ppt,const std::string& filename)
{
  PlayTree *pt = *ppt, *entry = new(zeromem) PlayTree;

  entry->add_file(filename);
  if (pt)
    pt->append_entry(entry);
  else
  {
    pt=entry;
    *ppt=pt;
  }
  entry->set_params_from(*pt);
}

void pt_add_gui_file(PlayTree** ppt,const std::string& path,const std::string& file)
{
    std::string wholename;

    wholename=path+"/"+file;
    pt_add_file(ppt, wholename);
}

void pt_iter_goto_head(_PlayTree_Iter* iter)
{
  iter->reset_tree();
  iter->step(0, 0);
}
