/* $Id: block_alloc.h,v 1.9 1999/04/02 20:53:12 hubbe Exp $ */
#undef PRE_INIT_BLOCK
#undef INIT_BLOCK
#undef EXIT_BLOCK
#undef BLOCK_ALLOC
#undef PTR_HASH_ALLOC

#define PRE_INIT_BLOCK(X)
#define INIT_BLOCK(X)
#define EXIT_BLOCK(X)

#define BLOCK_ALLOC(DATA,BSIZE)						\
									\
struct PIKE_CONCAT(DATA,_block)						\
{									\
  struct PIKE_CONCAT(DATA,_block) *next;				\
  struct DATA x[BSIZE];							\
};									\
									\
static struct PIKE_CONCAT(DATA,_block) *PIKE_CONCAT(DATA,_blocks)=0;	\
static struct DATA *PIKE_CONCAT3(free_,DATA,s)=0;			\
									\
struct DATA *PIKE_CONCAT(alloc_,DATA)(void)				\
{									\
  struct DATA *tmp;							\
  if(!PIKE_CONCAT3(free_,DATA,s))					\
  {									\
    struct PIKE_CONCAT(DATA,_block) *n;					\
    int e;								\
    n=(struct PIKE_CONCAT(DATA,_block) *)				\
       malloc(sizeof(struct PIKE_CONCAT(DATA,_block)));			\
    if(!n)								\
    {									\
      fprintf(stderr,"Fatal: out of memory.\n");			\
      exit(17);								\
    }									\
    n->next=PIKE_CONCAT(DATA,_blocks);					\
    PIKE_CONCAT(DATA,_blocks)=n;					\
									\
    for(e=0;e<BSIZE;e++)						\
    {									\
      n->x[e].next=PIKE_CONCAT3(free_,DATA,s);				\
      PRE_INIT_BLOCK( (n->x+e) );					\
      PIKE_CONCAT3(free_,DATA,s)=n->x+e;				\
    }									\
  }									\
									\
  tmp=PIKE_CONCAT3(free_,DATA,s);					\
  PIKE_CONCAT3(free_,DATA,s)=tmp->next;					\
  INIT_BLOCK(tmp);							\
  return tmp;								\
}									\
									\
void PIKE_CONCAT(really_free_,DATA)(struct DATA *d)			\
{									\
  EXIT_BLOCK(d);							\
  d->next=PIKE_CONCAT3(free_,DATA,s);					\
  PRE_INIT_BLOCK(d);							\
  PIKE_CONCAT3(free_,DATA,s)=d;						\
}									\
									\
void PIKE_CONCAT3(free_all_,DATA,_blocks)(void)				\
{									\
  struct PIKE_CONCAT(DATA,_block) *tmp;					\
  while((tmp=PIKE_CONCAT(DATA,_blocks)))				\
  {									\
    PIKE_CONCAT(DATA,_blocks)=tmp->next;				\
    free((char *)tmp);							\
  }									\
  PIKE_CONCAT(DATA,_blocks)=0;						\
  PIKE_CONCAT3(free_,DATA,s)=0;						\
}									\




#define PTR_HASH_ALLOC(DATA,BSIZE)					\
									\
BLOCK_ALLOC(DATA,BSIZE)							\
									\
static struct DATA **PIKE_CONCAT(DATA,_hash_table)=0;			\
static int PIKE_CONCAT(DATA,_hash_table_size)=0;			\
static int PIKE_CONCAT(num_,DATA)=0;					\
									\
inline struct DATA *							\
 PIKE_CONCAT(really_low_find_,DATA)(void *ptr, int hval)		\
{									\
  struct DATA *p,**pp;							\
  for(pp=PIKE_CONCAT(DATA,_hash_table)+hval;(p=*pp);pp=&p->next)	\
  {									\
    if(p->data==ptr)							\
    {									\
      *pp=p->next;							\
      p->next=PIKE_CONCAT(DATA,_hash_table)[hval];			\
      PIKE_CONCAT(DATA,_hash_table)[hval]=p;				\
      return p;								\
    }									\
  }									\
  return 0;								\
}									\
									\
									\
struct DATA *PIKE_CONCAT(find_,DATA)(void *ptr)				\
{									\
  int hval=(long)ptr;							\
  hval%=PIKE_CONCAT(DATA,_hash_table_size);				\
  return PIKE_CONCAT(really_low_find_,DATA)(ptr, hval);			\
}									\
									\
									\
									\
struct DATA *PIKE_CONCAT(make_,DATA)(void *ptr, int hval)	\
{									\
  struct DATA *p;							\
									\
  PIKE_CONCAT(num_,DATA)++;						\
									\
  if(( PIKE_CONCAT(num_,DATA)>>2 ) >=					\
     PIKE_CONCAT(DATA,_hash_table_size))				\
  {									\
    /* Time to re-hash */						\
    struct DATA **old_hash= PIKE_CONCAT(DATA,_hash_table);		\
    int e=PIKE_CONCAT(DATA,_hash_table_size);				\
									\
    PIKE_CONCAT(DATA,_hash_table_size)*=2;				\
    PIKE_CONCAT(DATA,_hash_table_size)++;				\
    if((PIKE_CONCAT(DATA,_hash_table)=(struct DATA **)			\
	malloc(PIKE_CONCAT(DATA,_hash_table_size)*			\
	       sizeof(struct DATA *))))					\
    {									\
      MEMSET(PIKE_CONCAT(DATA,_hash_table),0,				\
	 sizeof(struct DATA *)*PIKE_CONCAT(DATA,_hash_table_size));	\
      while(e-- >=0)							\
      {									\
	while((p=old_hash[e]))		        \
	{								\
	  old_hash[e]=p->next;                 \
	  hval=(long)(p-> data);					\
	  hval%=PIKE_CONCAT(DATA,_hash_table_size);			\
	  p->next=PIKE_CONCAT(DATA,_hash_table)[hval];			\
	  PIKE_CONCAT(DATA,_hash_table)[hval]=p;			\
	}								\
      }									\
      hval=(long)ptr;							\
      hval%=PIKE_CONCAT(DATA,_hash_table_size);				\
      free((char *)old_hash);\
    }else{								\
      PIKE_CONCAT(DATA,_hash_table)=old_hash;	\
      PIKE_CONCAT(DATA,_hash_table_size)=e;				\
    }									\
  }									\
									\
  p=PIKE_CONCAT(alloc_,DATA)();	        				\
  p->data=ptr; \
  p->next=PIKE_CONCAT(DATA,_hash_table)[hval];				\
  PIKE_CONCAT(DATA,_hash_table)[hval]=p;				\
  return p;								\
}									\
									\
inline struct DATA *PIKE_CONCAT(get_,DATA)(void *ptr)			\
{									\
  struct DATA *p;							\
  int hval=(long)ptr;							\
  hval%=PIKE_CONCAT(DATA,_hash_table_size);				\
  if((p=PIKE_CONCAT(really_low_find_,DATA)(ptr, hval)))			\
    return p;								\
									\
  return PIKE_CONCAT(make_,DATA)(ptr, hval);				\
}									\
									\
int PIKE_CONCAT3(check_,DATA,_semafore)(void *ptr)			\
{									\
  struct DATA *p;							\
  int hval=(long)ptr;							\
  hval%=PIKE_CONCAT(DATA,_hash_table_size);				\
  if((p=PIKE_CONCAT(really_low_find_,DATA)(ptr, hval)))			\
    return 0;								\
									\
  PIKE_CONCAT(make_,DATA)(ptr, hval);					\
  return 1;								\
}									\
									\
int PIKE_CONCAT(remove_,DATA)(void *ptr)				\
{									\
  struct DATA *p;							\
  int hval=(long)ptr;							\
  hval%=PIKE_CONCAT(DATA,_hash_table_size);				\
  if((p=PIKE_CONCAT(really_low_find_,DATA)(ptr, hval)))			\
  {									\
    PIKE_CONCAT(num_,DATA)--;						\
    if(PIKE_CONCAT(DATA,_hash_table)[hval]!=p) fatal("GAOssdf\n");      \
    PIKE_CONCAT(DATA,_hash_table)[hval]=p->next;			\
    PIKE_CONCAT(really_free_,DATA)(p);					\
    return 1;								\
  }									\
  return 0;								\
}									\
									\
void PIKE_CONCAT3(init_,DATA,_hash)(void)				\
{									\
  extern INT32 hashprimes[32];						\
  extern int my_log2(unsigned INT32 x);					\
  PIKE_CONCAT(DATA,_hash_table_size)=hashprimes[my_log2(BSIZE)];	\
									\
  PIKE_CONCAT(DATA,_hash_table)=(struct DATA **)			\
    xalloc(sizeof(struct DATA *)*PIKE_CONCAT(DATA,_hash_table_size));	\
  MEMSET(PIKE_CONCAT(DATA,_hash_table),0,				\
	 sizeof(struct DATA *)*PIKE_CONCAT(DATA,_hash_table_size));	\
}									\
									\
void PIKE_CONCAT3(exit_,DATA,_hash)(void)				\
{									\
  PIKE_CONCAT3(free_all_,DATA,_blocks)();				\
  free(PIKE_CONCAT(DATA,_hash_table));					\
  PIKE_CONCAT(DATA,_hash_table)=0;					\
}									\

