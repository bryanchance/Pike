/*\
||| This file a part of Pike, and is copyright by Fredrik Hubinette
||| Pike is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/
#ifndef STRALLOC_H
#define STRALLOC_H
#include "types.h"

#define STRINGS_ARE_SHARED

struct pike_string
{
  SIZE_T refs;
  INT32 len;
  unsigned INT32 hval;
  struct pike_string *next; 
  char str[1];
};

#ifdef DEBUG
struct pike_string *debug_findstring(const struct pike_string *foo);
#endif

#define free_string(s) do{ struct pike_string *_=(s); if(!--_->refs) really_free_string(_); }while(0)

#define my_hash_string(X) ((unsigned long)(X))
#define my_order_strcmp(X,Y) ((char *)(X)-(char *)(Y))
#define is_same_string(X,Y) ((X)==(Y))

#define reference_shared_string(s) (s)->refs++
#define copy_shared_string(to,s) ((to)=(s))->refs++

/* Prototypes begin here */
void check_string(struct pike_string *s);
void verify_shared_strings_tables();
struct pike_string *binary_findstring(const char *foo, INT32 len);
struct pike_string *findstring(const char *foo);
struct pike_string *debug_findstring(const struct pike_string *foo);
struct pike_string *begin_shared_string(int len);
struct pike_string *end_shared_string(struct pike_string *s);
struct pike_string * make_shared_binary_string(const char *str,int len);
struct pike_string *make_shared_string(const char *str);
int low_quick_binary_strcmp(char *a,INT32 alen,
			    char *b,INT32 blen);
int my_quick_strcmp(struct pike_string *a,struct pike_string *b);
int my_strcmp(struct pike_string *a,struct pike_string *b);
void really_free_string(struct pike_string *s);
struct pike_string *add_string_status(int verbose);
void dump_stralloc_strings();
struct pike_string *add_shared_strings(struct pike_string *a,
					 struct pike_string *b);
struct pike_string *string_replace(struct pike_string *str,
				     struct pike_string *del,
				     struct pike_string *to);
void cleanup_shared_string_table();
/* Prototypes end here */

#endif /* STRALLOC_H */
