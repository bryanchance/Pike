/*\
||| This file a part of Pike, and is copyright by Fredrik Hubinette
||| Pike is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/

#include "global.h"

struct callback *gc_evaluator_callback=0;

#include "array.h"
#include "multiset.h"
#include "mapping.h"
#include "object.h"
#include "program.h"
#include "stralloc.h"
#include "stuff.h"
#include "error.h"
#include "pike_memory.h"
#include "pike_macros.h"
#include "pike_types.h"
#include "time_stuff.h"

#include "gc.h"
#include "main.h"
#include <math.h>

/* Run garbage collect approximate every time we have
 * 20 percent of all arrays, objects and programs is
 * garbage.
 */

#define GC_CONST 20
#define MIN_ALLOC_THRESHOLD 1000
#define MAX_ALLOC_THRESHOLD 10000000
#define MULTIPLIER 0.9
#define MARKER_CHUNK_SIZE 1023

INT32 num_objects =0;
INT32 num_allocs =0;
INT32 alloc_threshold = MIN_ALLOC_THRESHOLD;
static int in_gc = 0;

static double objects_alloced = 0.0;
static double objects_freed = 0.0;

struct callback_list gc_callbacks;

struct callback *add_gc_callback(callback_func call,
				 void *arg,
				 callback_func free_func)
{
  return add_to_callback(&gc_callbacks, call, arg, free_func);
}

#define GC_REFERENCED 1
#define GC_XREFERENCED 2

struct marker
{
  INT32 refs;
#ifdef DEBUG
  INT32 xrefs;
#endif
  INT32 flags;
  struct marker *next;
  void *marked;
};

struct marker_chunk
{
  struct marker_chunk *next;
  struct marker markers[MARKER_CHUNK_SIZE];
};

static struct marker_chunk *chunk=0;
static int markers_left_in_chunk=0;

static struct marker *new_marker(void)
{
  if(!markers_left_in_chunk)
  {
    struct marker_chunk *m;
    m=(struct marker_chunk *)xalloc(sizeof(struct marker_chunk));
    m->next=chunk;
    chunk=m;
    markers_left_in_chunk=MARKER_CHUNK_SIZE;
  }
  markers_left_in_chunk--;

  return chunk->markers + markers_left_in_chunk;
}

static struct marker **hash=0;
static unsigned long hashsize=0;

static struct marker *getmark(void *a)
{
  int hashval;
  struct marker *m;

  hashval=((unsigned long)a)%hashsize;

  for(m=hash[hashval];m;m=m->next)
    if(m->marked == a)
      return m;

  m=new_marker();
  m->marked=a;
  m->refs=0;
#ifdef DEBUG
  m->xrefs=0;
#endif
  m->flags=0;
  m->next=hash[hashval];
  hash[hashval]=m;

  return m;
}

#ifdef DEBUG

time_t last_gc;

void dump_gc_info(void)
{
  fprintf(stderr,"Current number of objects: %ld\n",(long)num_objects);
  fprintf(stderr,"Objects allocated total  : %ld\n",(long)num_allocs);
  fprintf(stderr," threshold for next gc() : %ld\n",(long)alloc_threshold);
  fprintf(stderr,"Average allocs per gc()  : %f\n",objects_alloced);
  fprintf(stderr,"Average frees per gc()   : %f\n",objects_freed);
  fprintf(stderr,"Second since last gc()   : %ld\n", (long)TIME(0) - (long)last_gc);
  fprintf(stderr,"Projected garbage        : %f\n", objects_freed * (double) num_allocs / (double) alloc_threshold);
  fprintf(stderr,"in_gc                    : %d\n", in_gc);
}

TYPE_T attempt_to_identify(void *something)
{
  struct array *a;
  struct object *o;
  struct program *p;

  a=&empty_array;
  do
  {
    if(a==(struct array *)something) return T_ARRAY;
    a=a->next;
  }while(a!=&empty_array);

  for(o=first_object;o;o=o->next)
    if(o==(struct object *)something)
      return T_OBJECT;

  for(p=first_program;p;p=p->next)
    if(p==(struct program *)something)
      return T_PROGRAM;

  return T_UNKNOWN;
}

static void *check_for =0;
static char *found_where="";
static void *found_in=0;
static int found_in_type=0;
void *gc_svalue_location=0;

void describe_location(void *memblock, TYPE_T type, void *location)
{
  if(!location) return;
  fprintf(stderr,"**Location of (short) svalue: %p\n",location);
  if(type==T_OBJECT)
  {
    struct object *o=(struct object *)memblock;
    if(o->prog)
    {
      INT32 e,d;
      for(e=0;e<(INT32)o->prog->num_inherits;e++)
      {
	struct inherit tmp=o->prog->inherits[e];
	char *base=o->storage + tmp.storage_offset;

	for(d=0;d<(INT32)tmp.prog->num_identifiers;d++)
	{
	  struct identifier *id=tmp.prog->identifiers+d;
	  if(!IDENTIFIER_IS_VARIABLE(id->identifier_flags)) continue;

	  if(location == (void *)(base + id->func.offset))
	  {
	    fprintf(stderr,"**In variable %s\n",id->name->str);
	  }
	}
      }
    }
    return;
  }

  if(type == T_ARRAY)
  {
    struct array *a=(struct array *)memblock;
    struct svalue *s=(struct svalue *)location;
    fprintf(stderr,"**In index %ld\n",(long)(s-ITEM(a)));
    return;
  }
}

static void gdb_gc_stop_here(void *a)
{
  fprintf(stderr,"***One ref found%s.\n",found_where);
  describe_something(found_in, found_in_type);
  describe_location(found_in, found_in_type, gc_svalue_location);
}

void debug_gc_xmark_svalues(struct svalue *s, int num, char *fromwhere)
{
  found_in=(void *)fromwhere;
  found_in_type=-1;
  gc_xmark_svalues(s,num);
  found_in_type=T_UNKNOWN;
  found_in=0;
}

TYPE_FIELD debug_gc_check_svalues(struct svalue *s, int num, TYPE_T t, void *data)
{
  TYPE_FIELD ret;
  found_in=data;
  found_in_type=t;
  ret=gc_check_svalues(s,num);
  found_in_type=T_UNKNOWN;
  found_in=0;
  return ret;
}

void debug_gc_check_short_svalue(union anything *u, TYPE_T type, TYPE_T t, void *data)
{
  found_in=data;
  found_in_type=t;
  gc_check_short_svalue(u,type);
  found_in_type=T_UNKNOWN;
  found_in=0;
}

void describe_something(void *a, int t)
{
  struct program *p=(struct program *)a;
  if(!a) return;
  if(t==-1)
  {
    fprintf(stderr,"**Location description: %s\n",(char *)a);
    return;
  }

  fprintf(stderr,"**Location: %p  Type: %s  Refs: %d\n",a,
	  get_name_of_type(t),
	  *(INT32 *)a);

  switch(t)
  {
    case T_OBJECT:
      p=((struct object *)a)->prog;
      if(!p)
      {
	fprintf(stderr,"**The object is destructed.\n");
	break;
      }
      fprintf(stderr,"**Attempting to describe program object was instantiated from:\n");
      
    case T_PROGRAM:
    {
      char *tmp;
      INT32 line,pos;

      fprintf(stderr,"**Program id: %ld\n",(long)(p->id));
      if(!p->num_linenumbers)
      {
	fprintf(stderr,"**The program was written in C.\n");
	break;
      }

      for(pos=0;pos<(long)p->program_size && pos<100;pos++)
      {
	tmp=get_line(p->program+pos, p, &line);
	if(tmp && line)
	{
	  fprintf(stderr,"**Location: %s:%ld\n",tmp,(long)line);
	  break;
	}
      }
      break;
    }
      
    case T_ARRAY:
      fprintf(stderr,"**Describing array:\n");
      debug_dump_array((struct array *)a);
      break;
  }
}

#endif

INT32 gc_check(void *a)
{
#ifdef DEBUG
  if(check_for)
  {
    if(check_for == a)
    {
      gdb_gc_stop_here(a);
    }
    return 0;
  }
#endif
  return getmark(a)->refs++;
}

int gc_is_referenced(void *a)
{
  struct marker *m;
  m=getmark(a);
#ifdef DEBUG
  if(m->refs + m->xrefs > *(INT32 *)a)
  {
    INT32 refs=m->refs;
    INT32 xrefs=m->xrefs;
    TYPE_T t=attempt_to_identify(a);

    fprintf(stderr,"**Something has %ld references, while gc() found %ld + %ld external.\n",(long)*(INT32 *)a,(long)refs,(long)xrefs);
    describe_something(a, t);

    fprintf(stderr,"**Looking for references:\n");
    check_for=a;

    found_where=" in an array";
    gc_check_all_arrays();

    found_where=" in a multiset";
    gc_check_all_multisets();

    found_where=" in a mapping";
    gc_check_all_mappings();

    found_where=" in a program";
    gc_check_all_programs();

    found_where=" in an object";
    gc_check_all_objects();

    found_where=" in a module";
    call_callback(& gc_callbacks, (void *)0);

    found_where="";
    check_for=0;

    fatal("Ref counts are wrong (has %d, found %d + %d external)\n",
	  *(INT32 *)a,
	  refs,
	  xrefs);
  }
#endif
  return m->refs < *(INT32 *)a;
}

#ifdef DEBUG
int gc_external_mark(void *a)
{
  struct marker *m;
  if(check_for)
  {
    if(a==check_for)
    {
      char *tmp=found_where;
      found_where=" externally";
      gdb_gc_stop_here(a);
      found_where=tmp;

      return 1;
    }
    return 0;
  }
  m=getmark(a);
  m->xrefs++;
  m->flags|=GC_XREFERENCED;
  gc_is_referenced(a);
  return 0;
}
#endif

int gc_mark(void *a)
{
  struct marker *m;
  m=getmark(a);

  if(m->flags & GC_REFERENCED)
  {
    return 0;
  }else{
    m->flags |= GC_REFERENCED;
    return 1;
  }
}

int gc_do_free(void *a)
{
  struct marker *m;
  m=getmark(a);
  return !(m->flags & GC_REFERENCED);
}

/* Not all of these are primes, but they should be adequate */
static INT32 hashprimes[] =
{
  31,        /* ~ 2^0  = 1 */
  31,        /* ~ 2^1  = 2 */
  31,        /* ~ 2^2  = 4 */
  31,        /* ~ 2^3  = 8 */
  31,        /* ~ 2^4  = 16 */
  31,        /* ~ 2^5  = 32 */
  61,        /* ~ 2^6  = 64 */
  127,       /* ~ 2^7  = 128 */
  251,       /* ~ 2^8  = 256 */
  541,       /* ~ 2^9  = 512 */
  1151,      /* ~ 2^10 = 1024 */
  2111,      /* ~ 2^11 = 2048 */
  4327,      /* ~ 2^12 = 4096 */
  8803,      /* ~ 2^13 = 8192 */
  17903,     /* ~ 2^14 = 16384 */
  32321,     /* ~ 2^15 = 32768 */
  65599,     /* ~ 2^16 = 65536 */
  133153,    /* ~ 2^17 = 131072 */
  270001,    /* ~ 2^18 = 264144 */
  547453,    /* ~ 2^19 = 524288 */
  1109891,   /* ~ 2^20 = 1048576 */
  2000143,   /* ~ 2^21 = 2097152 */
  4561877,   /* ~ 2^22 = 4194304 */
  9248339,   /* ~ 2^23 = 8388608 */
  16777215,  /* ~ 2^24 = 16777216 */
  33554431,  /* ~ 2^25 = 33554432 */
  67108863,  /* ~ 2^26 = 67108864 */
  134217727, /* ~ 2^27 = 134217728 */
  268435455, /* ~ 2^28 = 268435456 */
  536870911, /* ~ 2^29 = 536870912 */
  1073741823,/* ~ 2^30 = 1073741824 */
  2147483647,/* ~ 2^31 = 2147483648 */
};

void do_gc(void)
{
  double tmp;
  INT32 tmp2,tmp3;
  struct marker_chunk *m;
  double multiplier;

  if(in_gc) return;
  in_gc=1;

  if(gc_evaluator_callback)
  {
    remove_callback(gc_evaluator_callback);
    gc_evaluator_callback=0;
  }

  tmp2=num_objects;

#ifdef DEBUG
  if(t_flag)
    fprintf(stderr,"Garbage collecting ... ");
  if(num_objects < 0)
    fatal("Panic, less than zero objects!\n");

  last_gc=TIME(0);

#endif

  multiplier=pow(MULTIPLIER, (double) num_allocs / (double) alloc_threshold);
  objects_alloced*=multiplier;
  objects_alloced += (double) num_allocs;
  
  objects_freed*=multiplier;
  objects_freed += (double) num_objects;


  /* init hash , hashsize will be a prime between num_objects/8 and
   * num_objects/4, this will assure that no re-hashing is needed.
   */
  tmp3=my_log2(num_objects);

  if(!d_flag) tmp3-=2;
  if(tmp3<0) tmp3=0;
  if(tmp3>=(long)NELEM(hashprimes)) tmp3=NELEM(hashprimes)-1;
  hashsize=hashprimes[tmp3];

  hash=(struct marker **)xalloc(sizeof(struct marker **)*hashsize);
  MEMSET((char *)hash,0,sizeof(struct marker **)*hashsize);
  markers_left_in_chunk=0;

  /* First we count internal references */
  gc_check_all_arrays();
  gc_check_all_multisets();
  gc_check_all_mappings();
  gc_check_all_programs();
  gc_check_all_objects();
  call_callback(& gc_callbacks, (void *)0);

  /* Next we mark anything with external references */
  gc_mark_all_arrays();
  gc_mark_all_multisets();
  gc_mark_all_mappings();
  gc_mark_all_programs();
  gc_mark_all_objects();

  if(d_flag)
    gc_mark_all_strings();

  /* Now we free the unused stuff */
  gc_free_all_unreferenced_arrays();
  gc_free_all_unreferenced_multisets();
  gc_free_all_unreferenced_mappings();
  gc_free_all_unreferenced_programs();
  gc_free_all_unreferenced_objects();


  /* Free hash table */
  free((char *)hash);
  while((m=chunk))
  {
    chunk=m->next;
    free((char *)m);
  }

  destruct_objects_to_destruct();
  
  objects_freed -= (double) num_objects;

  tmp=(double)num_objects;
  tmp=tmp * GC_CONST/100.0 * (objects_alloced+1.0) / (objects_freed+1.0);
  
  if(alloc_threshold + num_allocs <= tmp)
    tmp = (double)(alloc_threshold + num_allocs);

  if(tmp < MIN_ALLOC_THRESHOLD)
    tmp = (double)MIN_ALLOC_THRESHOLD;
  if(tmp > MAX_ALLOC_THRESHOLD)
    tmp = (double)MAX_ALLOC_THRESHOLD;

  alloc_threshold = (int)tmp;

  num_allocs=0;

#ifdef DEBUG
  if(t_flag)
    fprintf(stderr,"done (freed %ld of %ld objects).\n",
	    (long)(tmp2-num_objects),(long)tmp2);
#endif

#ifdef ALWAYS_GC
  ADD_GC_CALLBACK();
#else
  if(d_flag > 3) ADD_GC_CALLBACK();
#endif
  in_gc=0;
}


