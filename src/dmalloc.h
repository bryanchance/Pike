#ifdef DEBUG_MALLOC

struct memhdr;

void dump_memhdr_locations(struct memhdr *from,
			   struct memhdr *notfrom);
struct memhdr *alloc_memhdr(void);
void free_memhdr(struct memhdr *mh);
void add_marks_to_memhdr(struct memhdr *to,void *ptr);
void low_add_marks_to_memhdr(struct memhdr *to,
			     struct memhdr *from);

extern int verbose_debug_malloc;
extern int verbose_debug_exit;
extern void *debug_malloc(size_t, const char *, int);
extern char *debug_xalloc(long);
extern void *debug_calloc(size_t, size_t, const char *, int);
extern void *debug_realloc(void *, size_t, const char *, int);
extern void debug_free(void *, const char *, int);
extern char *debug_strdup(const char *, const char *, int);
void *debug_malloc_update_location(void *,const char *, int);
void *debug_malloc_track(void *m, size_t s, const char *fn, int line);
void debug_malloc_untrack(void *p, const char *fn, int line);
#define malloc(x) debug_malloc((x), __FILE__, __LINE__)
#define calloc(x, y) debug_calloc((x), (y), __FILE__, __LINE__)
#define realloc(x, y) debug_realloc((x), (y), __FILE__, __LINE__)
#define free(x) debug_free((x), __FILE__, __LINE__)
#define strdup(x) debug_strdup((x), __FILE__, __LINE__)
#define DO_IF_DMALLOC(X) X
#define debug_malloc_touch(X) debug_malloc_update_location((X),__FILE__,__LINE__)
#define debug_malloc_pass(X) debug_malloc_update_location((X),__FILE__,__LINE__)
#define xalloc(X) ((char *)debug_malloc_touch(debug_xalloc(X)))
#define dmalloc_track(X) debug_malloc_track((X), 0 ,__FILE__,__LINE__)
#define dmalloc_untrack(X) debug_malloc_untrack((X),__FILE__,__LINE__)
void dmalloc_dump_track(void *p);
#else
#define dmalloc_dump_track(X)
#define dmalloc_track(X)
#define dmalloc_untrack(X)
#define xalloc debug_xalloc
#define dbm_main main
#define DO_IF_DMALLOC(X)
#define debug_malloc_update_location(X,Y,Z) (X)
#define debug_malloc_touch(X)
#define debug_malloc_pass(X) (X)
#endif
