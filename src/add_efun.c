/*\
||| This file a part of uLPC, and is copyright by Fredrik Hubinette
||| uLPC is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/
#include <stdlib.h>
#include "add_efun.h"
#include "macros.h"
#include "program.h"
#include "lpc_types.h"
#include "stralloc.h"
#include "memory.h"
#include "interpret.h"

static struct hash_table *efun_hash = 0;

struct efun *lookup_efun(struct lpc_string *name)
{
  struct hash_entry *h;

  if(!efun_hash) return 0;
  h=hash_lookup(efun_hash, name);
  if(!h) return 0;
  return BASEOF(h, efun, link);
}

void low_add_efun(struct lpc_string *name, struct svalue *fun)
{
  struct efun *parent;
  
  parent=lookup_efun(name);

  if(!parent)
  {
    if(!fun) return;

    parent=ALLOC_STRUCT(efun);
    copy_shared_string(parent->link.s,name);
    efun_hash=hash_insert(efun_hash, &parent->link);
  }else{
    free_svalue(& parent->function);

    /* Disable efun */
    if(!fun)
    {
      efun_hash=hash_unlink(efun_hash, &parent->link);
      free_string(parent->link.s);
      free((char *)parent);
      return;
    }
  }

  assign_svalue_no_free(& parent->function, fun);
}


struct callable *make_callable(c_fun fun,char *name, char *type, INT16 flags)
{
  struct callable *f;
  f=ALLOC_STRUCT(callable);
  f->refs=1;
  f->function=fun;
  f->name=make_shared_string(name);
  f->type=parse_type(type);
  f->flags=flags;
  return f;
}

void really_free_callable(struct callable *fun)
{
  free_string(fun->type);
  free_string(fun->name);
  free((char *)fun);
}

void add_efun(char *name, c_fun fun, char *type, INT16 flags)
{
  struct svalue s;
  struct lpc_string *n;

  n=make_shared_string(name);
  s.type=T_FUNCTION;
  s.subtype=-1;
  s.u.efun=make_callable(fun, name, type, flags);
  low_add_efun(n, &s);
  free_svalue(&s);
  free_string(n);
}

static void push_efun_entry(struct hash_entry *h)
{
  struct efun *f;
  f=BASEOF(h, efun, link);
  push_string(f->link.s);
  f->link.s->refs++;
  copy_svalues_recursively_no_free(sp++,& f->function,1,0);
}

void push_all_efuns_on_stack()
{
  if(efun_hash)
    map_hashtable(efun_hash,push_efun_entry);
}

static void free_one_hashtable_entry(struct hash_entry *h)
{
  struct efun *f;
  f=BASEOF(h, efun, link);
  free_svalue(& f->function);
  free((char *)f);
}

void cleanup_added_efuns()
{
  if(efun_hash)
  {
    free_hashtable(efun_hash,free_one_hashtable_entry);
    efun_hash=0;
  }
  
}
