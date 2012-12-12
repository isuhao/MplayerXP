#ifndef __PLAYTREEPARSER_H
#define __PLAYTREEPARSER_H

#include "playtree.h"

typedef struct play_tree_parser {
  Stream* stream;
  char *buffer,*iter,*line;
  int buffer_size , buffer_end;
  int deep,keep;
} play_tree_parser_t;


play_tree_parser_t* play_tree_parser_new(Stream * stream,int deep);

void play_tree_parser_free(play_tree_parser_t* p);

play_tree_t* play_tree_parser_get_play_tree(libinput_t&libinput,play_tree_parser_t* p);

#endif
