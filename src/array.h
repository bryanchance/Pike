/*\
||| This file a part of Pike, and is copyright by Fredrik Hubinette
||| Pike is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/

/*
 * $Id: array.h,v 1.28 2000/08/16 15:54:28 grubba Exp $
 */
#ifndef ARRAY_H
#define ARRAY_H

#include "las.h"

struct array
{
  INT32 refs;		/* Reference count */
#ifdef PIKE_SECURITY
  struct object *prot;
#endif
  struct array *next;	/* we need to keep track of all arrays */
  struct array *prev;	/* Another pointer, so we don't have to search
			 * when freeing arrays */
  INT32 size;		/* number of items in this array */
  INT32 malloced_size;	/* number of elements that can fit in this array */
  TYPE_FIELD type_field;/* A bitfield with one bit for each type.
			 * Bits can be set that don't exist in the array
			 * though.
			 */
  INT16 flags;          /* ARRAY_* flags */
  struct svalue item[1];
};

#define ARRAY_WEAK_FLAG 1
#define ARRAY_CYCLIC 2
#define ARRAY_LVALUE 4
#define ARRAY_WEAK_SHRINK 8

extern struct array empty_array;
extern struct array *gc_internal_array;

#if defined(DEBUG_MALLOC) && defined(PIKE_DEBUG)
#define ITEM(X) (((struct array *)(debug_malloc_pass((X))))->item)
#else
#define ITEM(X) ((X)->item)
#endif

#define LINK_ARRAY(a) do {						\
  (a)->prev = &empty_array;						\
  (a)->next = empty_array.next;						\
  empty_array.next = (a);						\
  (a)->next->prev = (a);						\
} while (0)

#define UNLINK_ARRAY(a) do {						\
  struct array *next = (a)->next, *prev = (a)->prev;			\
  prev->next = next;							\
  next->prev = prev;							\
} while (0)

/* These are arguments for the function 'merge' which merges two sorted
 * set stored in arrays in the way you specify
 */
#define PIKE_ARRAY_OP_A 1
#define PIKE_ARRAY_OP_SKIP_A 2
#define PIKE_ARRAY_OP_TAKE_A 3
#define PIKE_ARRAY_OP_B 4
#define PIKE_ARRAY_OP_SKIP_B 8
#define PIKE_ARRAY_OP_TAKE_B 12
#define PIKE_MINTERM(X,Y,Z) (((X)<<8)+((Y)<<4)+(Z))

#define PIKE_ARRAY_OP_AND PIKE_MINTERM(PIKE_ARRAY_OP_SKIP_A,PIKE_ARRAY_OP_SKIP_A | PIKE_ARRAY_OP_TAKE_B,PIKE_ARRAY_OP_SKIP_B)
#define PIKE_ARRAY_OP_AND_LEFT PIKE_MINTERM(PIKE_ARRAY_OP_SKIP_A,PIKE_ARRAY_OP_SKIP_B | PIKE_ARRAY_OP_TAKE_A,PIKE_ARRAY_OP_SKIP_B)
#define PIKE_ARRAY_OP_OR  PIKE_MINTERM(PIKE_ARRAY_OP_TAKE_A,PIKE_ARRAY_OP_SKIP_A | PIKE_ARRAY_OP_TAKE_B,PIKE_ARRAY_OP_TAKE_B)
#define PIKE_ARRAY_OP_OR_LEFT  PIKE_MINTERM(PIKE_ARRAY_OP_TAKE_A,PIKE_ARRAY_OP_SKIP_B | PIKE_ARRAY_OP_TAKE_A,PIKE_ARRAY_OP_TAKE_B)
#define PIKE_ARRAY_OP_XOR PIKE_MINTERM(PIKE_ARRAY_OP_TAKE_A,PIKE_ARRAY_OP_SKIP_A | PIKE_ARRAY_OP_SKIP_B,PIKE_ARRAY_OP_TAKE_B)
#define PIKE_ARRAY_OP_ADD PIKE_MINTERM(PIKE_ARRAY_OP_TAKE_A,PIKE_ARRAY_OP_TAKE_A | PIKE_ARRAY_OP_TAKE_B ,PIKE_ARRAY_OP_TAKE_B)
#define PIKE_ARRAY_OP_SUB PIKE_MINTERM(PIKE_ARRAY_OP_TAKE_A,PIKE_ARRAY_OP_SKIP_A ,PIKE_ARRAY_OP_SKIP_B)


#define free_array(V) do{ struct array *v_=(V); debug_malloc_touch(v_); if(!--v_->refs) really_free_array(v_); }while(0)

#define allocate_array(X) low_allocate_array((X),0)
#define allocate_array_no_init(X,Y) low_allocate_array((X),(Y))

typedef int (*cmpfun)(struct svalue *,struct svalue *);
typedef int (*short_cmpfun)(union anything *, union anything *);
typedef short_cmpfun (*cmpfun_getter)(TYPE_T);


/* Prototypes begin here */
PMOD_EXPORT struct array *low_allocate_array(ptrdiff_t size, ptrdiff_t extra_space);
void really_free_array(struct array *v);
PMOD_EXPORT void do_free_array(struct array *a);
PMOD_EXPORT void array_index_no_free(struct svalue *s,struct array *v,INT32 index);
PMOD_EXPORT void array_index(struct svalue *s,struct array *v,INT32 index);
PMOD_EXPORT void simple_array_index_no_free(struct svalue *s,
				struct array *a,struct svalue *ind);
PMOD_EXPORT void array_free_index(struct array *v,INT32 index);
PMOD_EXPORT void array_set_index(struct array *v,INT32 index, struct svalue *s);
PMOD_EXPORT void simple_set_index(struct array *a,struct svalue *ind,struct svalue *s);
PMOD_EXPORT struct array *array_insert(struct array *v,struct svalue *s,INT32 index);
PMOD_EXPORT struct array *resize_array(struct array *a, INT32 size);
PMOD_EXPORT struct array *array_shrink(struct array *v, ptrdiff_t size);
PMOD_EXPORT struct array *array_remove(struct array *v,INT32 index);
PMOD_EXPORT ptrdiff_t array_search(struct array *v, struct svalue *s,
				   ptrdiff_t start);
PMOD_EXPORT struct array *slice_array(struct array *v, ptrdiff_t start,
				      ptrdiff_t end);
PMOD_EXPORT struct array *friendly_slice_array(struct array *v,
					       ptrdiff_t start,
					       ptrdiff_t end);
PMOD_EXPORT struct array *copy_array(struct array *v);
PMOD_EXPORT void check_array_for_destruct(struct array *v);
PMOD_EXPORT INT32 array_find_destructed_object(struct array *v);
INT32 *get_order(struct array *v, cmpfun fun);
PMOD_EXPORT void sort_array_destructively(struct array *v);
PMOD_EXPORT INT32 *get_set_order(struct array *a);
PMOD_EXPORT INT32 *get_switch_order(struct array *a);
PMOD_EXPORT INT32 *get_alpha_order(struct array *a);
INT32 set_lookup(struct array *a, struct svalue *s);
INT32 switch_lookup(struct array *a, struct svalue *s);
PMOD_EXPORT struct array *order_array(struct array *v, INT32 *order);
PMOD_EXPORT struct array *reorder_and_copy_array(struct array *v, INT32 *order);
PMOD_EXPORT void array_fix_type_field(struct array *v);
void array_check_type_field(struct array *v);
PMOD_EXPORT struct array *compact_array(struct array *v);
union anything *low_array_get_item_ptr(struct array *a,
				       INT32 ind,
				       TYPE_T t);
union anything *array_get_item_ptr(struct array *a,
				   struct svalue *ind,
				   TYPE_T t);
INT32 * merge(struct array *a,struct array *b,INT32 opcode);
PMOD_EXPORT struct array *array_zip(struct array *a, struct array *b,INT32 *zipper);
PMOD_EXPORT struct array *add_arrays(struct svalue *argp, INT32 args);
PMOD_EXPORT int array_equal_p(struct array *a, struct array *b, struct processing *p);
PMOD_EXPORT struct array *merge_array_with_order(struct array *a, struct array *b,INT32 op);
PMOD_EXPORT struct array *merge_array_without_order2(struct array *a, struct array *b,INT32 op);
PMOD_EXPORT struct array *merge_array_without_order(struct array *a,
					struct array *b,
					INT32 op);
PMOD_EXPORT struct array *subtract_arrays(struct array *a, struct array *b);
PMOD_EXPORT struct array *and_arrays(struct array *a, struct array *b);
int check_that_array_is_constant(struct array *a);
node *make_node_from_array(struct array *a);
PMOD_EXPORT void push_array_items(struct array *a);
void describe_array_low(struct array *a, struct processing *p, int indent);
PMOD_EXPORT void simple_describe_array(struct array *a);
void describe_index(struct array *a,
		    int e,
		    struct processing *p,
		    int indent);
void describe_array(struct array *a,struct processing *p,int indent);
struct array *aggregate_array(INT32 args);
PMOD_EXPORT struct array *append_array(struct array *a, struct svalue *s);
PMOD_EXPORT struct array *explode(struct pike_string *str,
		       struct pike_string *del);
PMOD_EXPORT struct pike_string *implode(struct array *a,struct pike_string *del);
PMOD_EXPORT struct array *copy_array_recursively(struct array *a,struct processing *p);
PMOD_EXPORT void apply_array(struct array *a, INT32 args);
PMOD_EXPORT struct array *reverse_array(struct array *a);
PMOD_EXPORT void array_replace(struct array *a,
		   struct svalue *from,
		   struct svalue *to);
PMOD_EXPORT void check_array(struct array *a);
void check_all_arrays(void);
void gc_mark_array_as_referenced(struct array *a);
void real_gc_cycle_check_array(struct array *a, int weak);
unsigned gc_touch_all_arrays(void);
void gc_check_all_arrays(void);
void gc_mark_all_arrays(void);
void gc_cycle_check_all_arrays(void);
void gc_zap_ext_weak_refs_in_arrays(void);
void gc_free_all_unreferenced_arrays(void);
void debug_dump_type_field(TYPE_FIELD t);
void debug_dump_array(struct array *a);
void zap_all_arrays(void);
void count_memory_in_arrays(INT32 *num_, INT32 *size_);
PMOD_EXPORT struct array *explode_array(struct array *a, struct array *b);
PMOD_EXPORT struct array *implode_array(struct array *a, struct array *b);
/* Prototypes end here */

#define gc_cycle_check_array(X, WEAK) \
  gc_cycle_enqueue((gc_cycle_check_cb *) real_gc_cycle_check_array, (X), (WEAK))

#endif /* ARRAY_H */
