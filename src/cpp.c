/*\
||| This file a part of Pike, and is copyright by Fredrik Hubinette
||| Pike is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/

/*
 * $Id: cpp.c,v 1.42 1999/02/24 23:27:11 grubba Exp $
 */
#include "global.h"
#include "language.h"
#include "lex.h"
#include "stralloc.h"
#include "module_support.h"
#include "interpret.h"
#include "svalue.h"
#include "pike_macros.h"
#include "hashtable.h"
#include "program.h"
#include "object.h"
#include "error.h"
#include "array.h"
#include "mapping.h"
#include "builtin_functions.h"
#include "operators.h"
#include "opcodes.h"
#include "constants.h"
#include "time.h"
#include "stuff.h"
#include <ctype.h>

#define CPP_NO_OUTPUT 1
#define CPP_EXPECT_ELSE 2
#define CPP_EXPECT_ENDIF 4
#define CPP_REALLY_NO_OUTPUT 8
#define CPP_END_AT_NEWLINE 16
#define CPP_DO_IF 32
#define CPP_NO_EXPAND 64

#define OUTP() (!(flags & (CPP_NO_OUTPUT | CPP_REALLY_NO_OUTPUT)))
#define PUTNL() string_builder_putchar(&this->buf, '\n')
#define GOBBLE(X) (data[pos]==(X)?++pos,1:0)
#define PUTC(C) do { \
 int c_=(C); if(OUTP() || c_=='\n') string_builder_putchar(&this->buf, c_); }while(0)

#define STRCAT(STR,LEN) do {				\
  INT32 x_,len_=(LEN);					\
  char *str_=(STR);					\
  if(OUTP())						\
    string_builder_binary_strcat(&this->buf, str_,len_);	\
  else							\
    for(x_=0;x_<len_;x_++)				\
      if(str_[x_]=='\n')				\
        string_builder_putchar(&this->buf, '\n');		\
}while(0)

#define CHECKWORD(X) \
 (!strncmp(X,data+pos,strlen(X)) && !isidchar(data[pos+strlen(X)]))
#define WGOBBLE(X) (CHECKWORD(X) ? (pos+=strlen(X)),1 : 0)
#define GOBBLEOP(X) \
 ((!strncmp(X,data+pos,strlen(X))) ? (pos+=strlen(X)),1 : 0)

#define MAX_ARGS            255
#define DEF_ARG_STRINGIFY   0x100000
#define DEF_ARG_NOPRESPACE  0x200000
#define DEF_ARG_NOPOSTSPACE 0x400000
#define DEF_ARG_MASK        0x0fffff


struct pike_predef_s
{
  struct pike_predef_s *next;
  char *name;
  char *value;
};

static struct pike_predef_s *pike_predefs=0;
struct define_part
{
  int argument;
  struct pike_string *postfix;
};

struct define_argument {
  char *arg;
  INT32 len;
};


struct cpp;
struct define;
typedef void (*magic_define_fun)(struct cpp *,
				 struct define *,
				 struct define_argument *,
				 struct string_builder *);


struct define
{
  struct hash_entry link; /* must be first */
  magic_define_fun magic;
  int args;
  int num_parts;
  int inside;
  struct pike_string *first;
  struct define_part parts[1];
};

#define find_define(N) \
  (this->defines?BASEOF(hash_lookup(this->defines, N), define, link):0)

struct cpp
{
  struct hash_table *defines;
  INT32 current_line;
  INT32 compile_errors;
  struct pike_string *current_file;
  struct string_builder buf;
};

struct define *defined_macro =0;
struct define *constant_macro =0;

static INT32 calc(struct cpp *,char*,INT32,INT32);
static INT32 calc1(struct cpp *,char*,INT32,INT32);

void cpp_error(struct cpp *this,char *err)
{
  this->compile_errors++;
  if(this->compile_errors > 10) return;
  if(get_master())
  {
    ref_push_string(this->current_file);
    push_int(this->current_line);
    push_text(err);
    SAFE_APPLY_MASTER("compile_error",3);
    pop_stack();
  }else{
    (void)fprintf(stderr, "%s:%ld: %s\n",
		  this->current_file->str,
		  (long)this->current_line,
		  err);
    fflush(stderr);
  }
}

/* devours one reference to 'name'! */
static struct define *alloc_empty_define(struct pike_string *name, INT32 parts)
{
  struct define *def;
  def=(struct define *)xalloc(sizeof(struct define)+
			      sizeof(struct define_part) * (parts -1));
  def->magic=0;
  def->args=-1;
  def->inside=0;
  def->num_parts=parts;
  def->first=0;
  def->link.s=name;
  return def;
}

static void undefine(struct cpp *this,
		     struct pike_string *name)
{
  INT32 e;
  struct define *d;

  d=find_define(name);

  if(!d) return;

  this->defines=hash_unlink(this->defines, & d->link);

  for(e=0;e<d->num_parts;e++)
    free_string(d->parts[e].postfix);
  free_string(d->link.s);
  if(d->first)
    free_string(d->first);
  free((char *)d);
}

static void do_magic_define(struct cpp *this,
			    char *name,
			    magic_define_fun fun)
{
  struct define* def=alloc_empty_define(make_shared_string(name),0);
  def->magic=fun;
  this->defines=hash_insert(this->defines, & def->link);
}

static void simple_add_define(struct cpp *this,
			    char *name,
			    char *what)
{
  struct define* def=alloc_empty_define(make_shared_string(name),0);
  def->first=make_shared_string(what);
  this->defines=hash_insert(this->defines, & def->link);
}


/* Who needs inline functions anyway? /Hubbe */

#define FIND_END_OF_STRING() do {					\
  while(1)								\
  {									\
    if(pos>=len)							\
    {									\
      cpp_error(this,"End of file in string.");				\
      break;								\
    }									\
    switch(data[pos++])							\
    {									\
    case '\n':								\
      cpp_error(this,"Newline in string.");				\
      this->current_line++;						\
      break;								\
    case '"': break;							\
    case '\\': if(data[++pos]=='\n') this->current_line++;		\
    default: continue;							\
    }									\
   break;								\
  } } while(0)


#define FIND_END_OF_CHAR() do {					\
  int e=0;							\
  while(1)							\
  {								\
    if(pos>=len)						\
    {								\
      cpp_error(this,"End of file in character constant.");	\
      break;							\
    }								\
								\
    if(e++>32)							\
    {								\
      cpp_error(this,"Too long character constant.");		\
      break;							\
    }								\
								\
    switch(data[pos++])						\
    {								\
    case '\n':							\
      cpp_error(this,"Newline in char.");			\
      this->current_line++;					\
      break;							\
    case '\'': break;						\
    case '\\': if(data[++pos]=='\n') this->current_line++;	\
    default: continue;						\
    }								\
    break;							\
  } } while(0)

#define DUMPPOS(X)					\
		  fprintf(stderr,"\nPOS(%s):",X);	\
		  fflush(stderr);			\
		  write(2,data+pos,20);			\
		  fprintf(stderr,"\n");			\
		  fflush(stderr)

#define FIND_EOL() do {				\
    while(pos < len && data[pos]!='\n') pos++;	\
  } while(0)

#define SKIPWHITE() do {					\
    if(!isspace(((unsigned char *)data)[pos])) break;		\
    if(data[pos]=='\n') { PUTNL(); this->current_line++; }	\
    pos++;							\
  } while(1)

#define SKIPSPACE() \
  do { while(isspace(((unsigned char *)data)[pos]) && data[pos]!='\n') pos++; \
  } while (0)

#define SKIPCOMMENT()	do{				\
  	pos++;						\
	while(data[pos]!='*' || data[pos+1]!='/')	\
	{						\
	  if(pos+2>=len)				\
	  {						\
	    cpp_error(this,"End of file in comment.");	\
	    break;					\
	  }						\
							\
	  if(data[pos]=='\n')				\
	  {						\
	    this->current_line++;			\
	    PUTNL();					\
	  }						\
							\
	  pos++;					\
	}						\
	pos+=2;						\
  }while(0)

#define READCHAR(C) do {			\
  switch(data[++pos])				\
  {						\
  case 'n': C='\n'; break;			\
  case 'r': C='\r'; break;			\
  case 'b': C='\b'; break;			\
  case 't': C='\t'; break;			\
    						\
  case '0': case '1': case '2': case '3':	\
  case '4': case '5': case '6': case '7':	\
    C=data[pos]-'0';				\
    if(data[pos+1]>='0' && data[pos+1]<='8')	\
    {						\
      C*=8;					\
      C+=data[++pos]-'0';			\
      						\
      if(data[pos+1]>='0' && data[pos+1]<='8')	\
      {						\
	C*=8;					\
	C+=data[++pos]-'0';			\
      }						\
    }						\
    break;					\
    						\
  case '\n':					\
    this->current_line++;			\
    C='\n';					\
    break;					\
    						\
  default:					\
    C=((unsigned char *)data)[pos];		\
  }						\
}while (0)

/* At entry pos points to the start-quote.
 * At end pos points past the end-quote.
 */
#define READSTRING(nf)				\
while(1)					\
{						\
  pos++;					\
  if(pos>=len)					\
  {						\
    cpp_error(this,"End of file in string.");	\
    break;					\
  }						\
						\
  switch(data[pos])				\
  {						\
  case '\n':					\
    cpp_error(this,"Newline in string.");	\
    this->current_line++;			\
    break;					\
  case '"':  break;				\
  case '\\':					\
  {						\
    int tmp;					\
    if(data[pos+1]=='\n')			\
    {						\
      pos++;					\
      continue;					\
    }						\
    READCHAR(tmp);				\
    string_builder_putchar(&nf, tmp);		\
    continue;					\
  }						\
						\
  default:					\
    string_builder_putchar(&nf, ((unsigned char *)data)[pos]);	\
    continue;					\
  }						\
  pos++;					\
  break;					\
}

/* At entry pos points past the start quote.
 * At exit pos points past the end quote.
 */
#define FIXSTRING(nf,outp)	do {			\
int trailing_newlines=0;				\
if(outp) string_builder_putchar(&nf, '"');		\
while(1)						\
{							\
  if(pos>=len)						\
  {							\
    cpp_error(this,"End of file in string.");		\
    break;						\
  }							\
							\
  switch(data[pos++])					\
  {							\
  case '\n':						\
    cpp_error(this,"Newline in string.");		\
    this->current_line++;				\
    break;						\
  case '"':  break;					\
  case '\\':						\
    if(data[pos]=='\n')					\
    {							\
      pos++;						\
      trailing_newlines++;				\
      this->current_line++;				\
      continue;						\
    }							\
    if(outp) string_builder_putchar(&nf, '\\');	        \
    pos++;                                              \
							\
  default:						\
    if(outp) string_builder_putchar(&nf, ((unsigned char *)data)[pos-1]);	\
    continue;						\
  }							\
  break;						\
}							\
if(outp) string_builder_putchar(&nf, '"');		\
while(trailing_newlines--) PUTNL();			\
}while(0)

#define READSTRING2(nf)				\
while(1)					\
{						\
  pos++;					\
  if(pos>=len)					\
  {						\
    cpp_error(this,"End of file in string.");	\
    break;					\
  }						\
						\
  switch(data[pos])				\
  {						\
  case '"':  break;				\
  case '\\':					\
  {						\
    int tmp;					\
    if(data[pos+1]=='\n')			\
    {						\
      pos++;					\
      continue;					\
    }						\
    READCHAR(tmp);				\
    string_builder_putchar(&nf, tmp);		\
    continue;					\
  }						\
						\
  case '\n':					\
    PUTNL();					\
    this->current_line++;			\
  default:					\
    string_builder_putchar(&nf, ((unsigned char *)data)[pos]);	\
    continue;					\
  }						\
  pos++;					\
  break;					\
}

void PUSH_STRING(char *str,
		 INT32 len,
		 struct string_builder *buf)
{
  INT32 p2;
  string_builder_putchar(buf, '"');
  for(p2=0;p2<len;p2++)
  {
    switch(str[p2])
    {
    case '\n':
      string_builder_putchar(buf, '\\');
      string_builder_putchar(buf, 'n');
      break;
      
    case '\t':
      string_builder_putchar(buf, '\\');
      string_builder_putchar(buf, 't');
      break;
      
    case '\r':
      string_builder_putchar(buf, '\\');
      string_builder_putchar(buf, 'r');
      break;
      
    case '\b':
      string_builder_putchar(buf, '\\');
      string_builder_putchar(buf, 'b');
      break;
      
    case '\\':
    case '"':
      string_builder_putchar(buf, '\\');
      string_builder_putchar(buf, ((unsigned char *)str)[p2]);
      break;
      
    default:
      if(isprint(EXTRACT_UCHAR(str+p2)))
      {
	string_builder_putchar(buf, ((unsigned char *)str)[p2]);
      }
      else
      {
	int c=EXTRACT_UCHAR(str+p2);
	string_builder_putchar(buf, '\\');
	string_builder_putchar(buf, ((c>>6)&7)+'0');
	string_builder_putchar(buf, ((c>>3)&7)+'0');
	string_builder_putchar(buf, (c&7)+'0');
	if(EXTRACT_UCHAR(str+p2+1)>='0' && EXTRACT_UCHAR(str+p2+1)<='7')
	{
	  string_builder_putchar(buf, '"');
	  string_builder_putchar(buf, '"');
	}
      }
      break;
    }
  }
  string_builder_putchar(buf, '"');
}

#define FINDTOK() 				\
  do {						\
  SKIPSPACE();					\
  if(data[pos]=='/')				\
  {						\
    INT32 tmp;					\
    switch(data[pos+1])				\
    {						\
    case '/':					\
      FIND_EOL();				\
      break;					\
      						\
    case '*':					\
      tmp=pos+2;				\
      while(1)					\
      {						\
        if(data[tmp]=='*')			\
	{					\
	  if(data[tmp+1] == '/')		\
	  {					\
	    pos=tmp+2;				\
	    break;				\
	  }					\
	  break;				\
	}					\
						\
	if(data[tmp]=='\n')			\
	  break;				\
        tmp++;					\
      }						\
    }						\
  }						\
  break;					\
  }while(1)

static INLINE int find_end_parenthesis(struct cpp *this,
				       char *data,
				       INT32 len,
				       INT32 pos) /* position of first " */
{
  while(1)
  {
    if(pos+1>=len)
    {
      cpp_error(this,"End of file while looking for end parenthesis.");
      return pos;
    }

    switch(data[pos++])
    {
    case '\n': PUTNL(); this->current_line++; break;
    case '\'': FIND_END_OF_CHAR();  break;
    case '"':  FIND_END_OF_STRING();  break;
    case '(':  pos=find_end_parenthesis(this, data, len, pos); break;
    case ')':  return pos;
    }
  }
}

static INT32 low_cpp(struct cpp *this,
		    char *data,
		    INT32 len,
		    int flags)
{
  INT32 pos, tmp, e, tmp2;
  
  for(pos=0;pos<len;)
  {
/*    fprintf(stderr,"%c",data[pos]);
    fflush(stderr); */

    switch(data[pos++])
    {
    case '\n':
      if(flags & CPP_END_AT_NEWLINE) return pos-1;

/*      fprintf(stderr,"CURRENT LINE: %d\n",this->current_line); */
      this->current_line++;
      PUTNL();
      goto do_skipwhite;

    case '\t':
    case ' ':
    case '\r':
      PUTC(' ');
      
    do_skipwhite:
      while(data[pos]==' ' || data[pos]=='\r' || data[pos]=='\t')
	pos++;
      break;

      /* Minor optimization */
      case '<':
	if(data[pos]=='<'  &&
	   data[pos+1]=='<'  &&
	   data[pos+2]=='<'  &&
	   data[pos+3]=='<'  &&
	   data[pos+4]=='<'  &&
	   data[pos+5]=='<')
	  cpp_error(this,"CVS conflict detected");
	
      case '!': case '@': case '$': case '%': case '^': case '&':
      case '*': case '(': case ')': case '-': case '=': case '+':
      case '{': case '}': case ':': case '?': case '`': case ';':
      case '>': case ',': case '.': case '~': case '[':
      case ']': case '|':
	PUTC(((unsigned char *)data)[pos-1]);
      break;
      
    default:
      if(OUTP() && isidchar(data[pos-1]))
      {
	struct pike_string *s=0;
	struct define *d=0;
	tmp=pos-1;
	while(isidchar(data[pos])) pos++;

	if(flags & CPP_DO_IF)
	{
	  if(pos-tmp == 7 && !strncmp("defined",data+tmp, 7))
	  {
	    d=defined_macro;
	  }
	  else if((pos-tmp == 4 && !strncmp("efun",data+tmp, 4)) ||
		  (pos-tmp == 8 && !strncmp("constant",data+tmp,8)))
	  {
	    d=constant_macro;
	  }
	  else
	  {
	    goto do_find_define;
	  }
	}else{
	do_find_define:
	  if((s=binary_findstring(data+tmp, pos-tmp)))
	  {
	    d=find_define(s);
	  }
	}
	  
	if(d && !d->inside)
	{
	  int arg=0;
	  struct string_builder tmp;
	  struct define_argument arguments [MAX_ARGS];
	  
	  if(s) add_ref(s);
	  
	  if(d->args>=0)
	  {
	    SKIPWHITE();
	    
	    if(!GOBBLE('('))
	    {
	      cpp_error(this,"Missing '(' in macro call.");
	      break;
	    }
	    
	    for(arg=0;arg<d->args;arg++)
	    {
	      if(arg && data[pos]==',')
	      {
		pos++;
		SKIPWHITE();
	      }else{
		SKIPWHITE();
		if(data[pos]==')')
		{
		  char buffer[1024];
		  sprintf(buffer,"Too few arguments to macro %s, expected %d.",d->link.s->str,d->args);
		  cpp_error(this,buffer);
		  break;
		}
	      }
	      arguments[arg].arg=data + pos;

	      
	      while(1)
	      {
		if(pos+1>len)
		{
		  cpp_error(this,"End of file in macro call.");
		  break;
		}
		
		switch(data[pos++])
		{
		case '\n':
		  this->current_line++;
		  PUTNL();
		default: continue;
		  
		case '"':
		  FIND_END_OF_STRING();
		  continue;
		  
		case '\'':
		  FIND_END_OF_CHAR();
		  continue;
		  
		case '(':
		  pos=find_end_parenthesis(this, data, len, pos);
		  continue;
		  
		case ')': 
		case ',': pos--;
		  break;
		}
		break;
	      }
	      arguments[arg].len=data+pos-arguments[arg].arg;
	    }
	    SKIPWHITE();
	    if(!GOBBLE(')'))
	      cpp_error(this,"Missing ) in macro call.");
	  }
	  
	  if(d->args >= 0 && arg != d->args)
	    cpp_error(this,"Wrong number of arguments to macro.");
	  
	  init_string_builder(&tmp, 0);
	  if(d->magic)
	  {
	    d->magic(this, d, arguments, &tmp);
	  }else{
	    string_builder_binary_strcat(&tmp, d->first->str, d->first->len);
	    for(e=0;e<d->num_parts;e++)
	    {
	      char *a;
	      INT32 l;
	      
	      if((d->parts[e].argument & DEF_ARG_MASK) < 0 || 
		 (d->parts[e].argument & DEF_ARG_MASK) >= arg)
	      {
		cpp_error(this,"Macro not expanded correctly.");
		continue;
	      }
	      
	      a=arguments[d->parts[e].argument&DEF_ARG_MASK].arg;
	      l=arguments[d->parts[e].argument&DEF_ARG_MASK].len;
	      
	      if(!(d->parts[e].argument & DEF_ARG_NOPRESPACE))
		string_builder_putchar(&tmp, ' ');
	      
	      if(d->parts[e].argument & DEF_ARG_STRINGIFY)
	      {
		PUSH_STRING(a,l,&tmp);
	      }else{
		if(DEF_ARG_NOPRESPACE)
		  while(l && isspace(*(unsigned char *)a))
		    a++,l--;
		
		if(DEF_ARG_NOPOSTSPACE)
		  while(l && isspace(*(unsigned char *)(a+l-1)))
		    l--;

		if(d->parts[e].argument & (DEF_ARG_NOPRESPACE | DEF_ARG_NOPOSTSPACE))
		{
		  
		  string_builder_binary_strcat(&tmp, a, l);
		}else{
		  struct string_builder save;
		  INT32 line=this->current_line;
		  save=this->buf;
		  this->buf=tmp;
		  low_cpp(this, a, l,
			  flags & ~(CPP_EXPECT_ENDIF | CPP_EXPECT_ELSE));
		  tmp=this->buf;
		  this->buf=save;
		  this->current_line=line;
		}
	      }
	      
	      if(!(d->parts[e].argument & DEF_ARG_NOPOSTSPACE))
		string_builder_putchar(&tmp, ' ');
	      
	      string_builder_binary_strcat(&tmp, d->parts[e].postfix->str,
					   d->parts[e].postfix->len);
	    }
	  }
	  
	  /* FIXME */
	  for(e=0;e<(long)tmp.s->len;e++)
	    if(tmp.s->str[e]=='\n')
	      tmp.s->str[e]=' ';

	  if(s) d->inside=1;
	  
	  string_builder_putchar(&tmp, 0);
	  tmp.s->len--;
	  
	  low_cpp(this, tmp.s->str, tmp.s->len, 
		  flags & ~(CPP_EXPECT_ENDIF | CPP_EXPECT_ELSE));
	  
	  if(s)
	  {
	    if((d=find_define(s)))
	      d->inside=0;
	    
	    free_string(s);
	  }

	  free_string_builder(&tmp);
	  break;
	}else{
	  if(flags & CPP_DO_IF)
	  {
	    STRCAT(" 0 ", 3);
	  }else{
	    STRCAT(data+tmp, pos-tmp);
	  }
	}
      }else{
	PUTC(((unsigned char *)data)[pos-1]);
      }
      break;
      
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      PUTC(((unsigned char *)data)[pos-1]);
      while(data[pos]>='0' && data[pos]<='9') PUTC(data[pos++]);
      break;

    case '"':
      FIXSTRING(this->buf,OUTP());
      break;

    case '\'':
      tmp=pos-1;
      FIND_END_OF_CHAR();
      STRCAT(data+tmp, pos-tmp);
      break;

    case '/':
      if(data[pos]=='/')
      {
	FIND_EOL();
	break;
      }

      if(data[pos]=='*')
      {
	PUTC(' ');
	SKIPCOMMENT();
	break;
      }

      PUTC(((unsigned char *)data)[pos-1]);
      break;

  case '#':
    if(GOBBLE('!'))
    {
      FIND_EOL();
      break;
    }
    SKIPSPACE();

    switch(data[pos])
    {
    case 'l':
      if(WGOBBLE("line"))
	{
	  /* FIXME: Why not use SKIPSPACE()? */
	  while(data[pos]==' ' || data[pos]=='\t') pos++;
	}else{
	  goto unknown_preprocessor_directive;
	}
      /* Fall through */
      
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
    {
      char *foo=data+pos;
      this->current_line=STRTOL(foo, &foo, 10)-1;
      string_builder_putchar(&this->buf, '#');
      string_builder_binary_strcat(&this->buf, data+pos, foo-(data+pos));
      pos=foo-data;
      SKIPSPACE();
      
      if(data[pos]=='"')
      {
	struct string_builder nf;
	init_string_builder(&nf, 0);
	
	READSTRING(nf);

	free_string(this->current_file);
	this->current_file = finish_string_builder(&nf);

	string_builder_putchar(&this->buf, ' ');
	PUSH_STRING(this->current_file->str,this->current_file->len,&this->buf);
      }
      
      FIND_EOL();
      break;
    }

      case '"':
      {
	struct string_builder nf;
	init_string_builder(&nf, 0);
	
	READSTRING2(nf);
	if(OUTP())
	{
	  PUSH_STRING(nf.s->str,
		      nf.s->len,
		      &this->buf);
	}
	free_string_builder(&nf);
	break;
      }

      case 's':
	if(WGOBBLE("string"))
	{
	  tmp2=1;
	  goto do_include;
	}
	
      goto unknown_preprocessor_directive;

    case 'i': /* include, if, ifdef */
      if(WGOBBLE("include"))
      {
	tmp2=0;
      do_include:
	{
	  struct svalue *save_sp=sp;
	  SKIPSPACE();
	  
	  check_stack(3);
	  
	  switch(data[pos++])
	  {
	    case '"':
	      {
		struct string_builder nf;
		init_string_builder(&nf, 0);
		pos--;
		READSTRING(nf);
		push_string(finish_string_builder(&nf));
		ref_push_string(this->current_file);
		push_int(1);
		break;
	      }

	    case '<':
	      {
		INT32 tmp=pos;
		while(data[pos]!='>')
		{
		  if(data[pos]=='\n')
		  {
		    cpp_error(this,"Expecting '>' in include.");
		    break;
		  }
		      
		  pos++;
		}
		push_string(make_shared_binary_string(data+tmp, pos-tmp));
		ref_push_string(this->current_file);
		pos++;
		push_int(0);
		break;
	      }

	    default:
	      cpp_error(this,"Expected file to include.");
	      break;
	    }

	  if(sp==save_sp) break;

	  if(OUTP())
	  {
	    struct pike_string *new_file;

	    SAFE_APPLY_MASTER("handle_include",3);
	  
	    if(sp[-1].type != T_STRING)
	    {
	      cpp_error(this,"Couldn't include file.");
	      pop_n_elems(sp-save_sp);
	      break;
	    }
	    
	    new_file=sp[-1].u.string;
	    
	    assign_svalue_no_free(sp,sp-1);
	    sp++;
	    
	    SAFE_APPLY_MASTER("read_include",1);
	    
	    if(sp[-1].type != T_STRING)
	    {
	      cpp_error(this,"Couldn't read include file.");
	      pop_n_elems(sp-save_sp);
	      break;
	    }
	    
	    {
	      char buffer[47];
	      struct pike_string *save_current_file;
	      INT32 save_current_line;

	      save_current_file=this->current_file;
	      save_current_line=this->current_line;
	      copy_shared_string(this->current_file,new_file);
	      this->current_line=1;
	      
	      string_builder_binary_strcat(&this->buf, "# 1 ", 4);
	      PUSH_STRING(new_file->str,new_file->len, & this->buf);
	      string_builder_putchar(&this->buf, '\n');
	      if(tmp2)
	      {
		PUSH_STRING(sp[-1].u.string->str,
			    sp[-1].u.string->len,
			    &this->buf);
	      }else{
		low_cpp(this,
			sp[-1].u.string->str,
			sp[-1].u.string->len,
			flags&~(CPP_EXPECT_ENDIF | CPP_EXPECT_ELSE));
	      }
	      
	      free_string(this->current_file);
	      this->current_file=save_current_file;
	      this->current_line=save_current_line;
	      
	      sprintf(buffer,"# %d ",this->current_line);
	      string_builder_binary_strcat(&this->buf, buffer, strlen(buffer));
	      PUSH_STRING(this->current_file->str,this->current_file->len,& this->buf);
	      string_builder_putchar(&this->buf, '\n');
	    }
	  }

	  pop_n_elems(sp-save_sp);
	  
	  break;
	}
      }

      if(WGOBBLE("if"))
      {
	struct string_builder save,tmp;
	INT32 nflags=CPP_EXPECT_ELSE | CPP_EXPECT_ENDIF;
	
	if(!OUTP())
	  nflags|=CPP_REALLY_NO_OUTPUT;
	
	save=this->buf;
	init_string_builder(&this->buf, 0);
	pos+=low_cpp(this,data+pos,len-pos,CPP_END_AT_NEWLINE|CPP_DO_IF);
	tmp=this->buf;
	this->buf=save;
	
	string_builder_putchar(&tmp, 0);
	tmp.s->len--;
	
	calc(this,tmp.s->str,tmp.s->len,0);
	free_string_builder(&tmp);
	if(IS_ZERO(sp-1)) nflags|=CPP_NO_OUTPUT;
	pop_stack();
	pos+=low_cpp(this,data+pos,len-pos,nflags);
	break;
      }

      if(WGOBBLE("ifdef"))
	{
	  INT32 namestart,nflags;
	  struct pike_string *s;
	  SKIPSPACE();

	  if(!isidchar(data[pos]))
	    cpp_error(this,"#ifdef what?\n");

	  namestart=pos;
	  while(isidchar(data[pos])) pos++;
	  nflags=CPP_EXPECT_ELSE | CPP_EXPECT_ENDIF | CPP_NO_OUTPUT;

	  if(!OUTP())
	    nflags|=CPP_REALLY_NO_OUTPUT;

	  if((s=binary_findstring(data+namestart,pos-namestart)))
	    if(find_define(s))
	      nflags&=~CPP_NO_OUTPUT;

	  pos+=low_cpp(this,data+pos,len-pos,nflags);
	  break;
	}

      if(WGOBBLE("ifndef"))
	{
	  INT32 namestart,nflags;
	  struct pike_string *s;
	  SKIPSPACE();

	  if(!isidchar(data[pos]))
	    cpp_error(this,"#ifndef what?");

	  namestart=pos;
	  while(isidchar(data[pos])) pos++;
	  nflags=CPP_EXPECT_ELSE | CPP_EXPECT_ENDIF;

	  if(!OUTP())
	    nflags|=CPP_REALLY_NO_OUTPUT;

	  if((s=binary_findstring(data+namestart,pos-namestart)))
	    if(find_define(s))
	      nflags|=CPP_NO_OUTPUT;

	  pos+=low_cpp(this,data+pos,len-pos,nflags);
	  break;
	}

      goto unknown_preprocessor_directive;

    case 'e': /* endif, else, elif, error */
      if(WGOBBLE("endif"))
      {
	if(!(flags & CPP_EXPECT_ENDIF))
	  cpp_error(this,"Unmatched #endif");

	return pos;
      }

      if(WGOBBLE("else"))
	{
	  if(!(flags & CPP_EXPECT_ELSE))
	    cpp_error(this,"Unmatched #else");

	  flags&=~CPP_EXPECT_ELSE;
	  flags|=CPP_EXPECT_ENDIF;

	  if(flags & CPP_NO_OUTPUT)
	    flags&=~CPP_NO_OUTPUT;
	  else
	    flags|=CPP_NO_OUTPUT;

	  break;
	}

      if(WGOBBLE("elif") || WGOBBLE("elseif"))
      {
	if(!(flags & CPP_EXPECT_ELSE))
	  cpp_error(this,"Unmatched #elif");
	
	flags|=CPP_EXPECT_ENDIF;
	
	if(flags & CPP_NO_OUTPUT)
	{
	  struct string_builder save,tmp;
	  save=this->buf;
	  init_string_builder(&this->buf, 0);
	  pos+=low_cpp(this,data+pos,len-pos,CPP_END_AT_NEWLINE|CPP_DO_IF);
	  tmp=this->buf;
	  this->buf=save;
	  
	  string_builder_putchar(&tmp, 0);
	  tmp.s->len--;
	  
	  calc(this,tmp.s->str,tmp.s->len,0);
	  free_string_builder(&tmp);
	  if(!IS_ZERO(sp-1)) flags&=~CPP_NO_OUTPUT;
	  pop_stack();
	}else{
	  FIND_EOL();
	  flags|= CPP_NO_OUTPUT | CPP_REALLY_NO_OUTPUT;
	}
	break;
      }

      if(WGOBBLE("error"))
	{
          INT32 foo;
          SKIPSPACE();
          foo=pos;
          FIND_EOL();
	  pos++;
	  if(OUTP())
	  {
	    push_string(make_shared_binary_string(data+foo,pos-foo));
	    cpp_error(this,sp[-1].u.string->str);
	  }
	  break;
	}
      
      goto unknown_preprocessor_directive;

    case 'd': /* define */
      if(WGOBBLE("define"))
	{
	  struct string_builder str;
	  INT32 namestart, tmp3, nameend, argno=-1;
	  struct define *def;
	  struct svalue *partbase,*argbase=sp;

	  SKIPSPACE();

	  namestart=pos;
	  if(!isidchar(data[pos]))
	    cpp_error(this,"Define what?");

	  while(isidchar(data[pos])) pos++;
	  nameend=pos;

	  if(GOBBLE('('))
	    {
	      argno=0;
	      SKIPWHITE();

	      while(data[pos]!=')')
		{
		  INT32 tmp2;
		  if(argno)
		    {
		      if(!GOBBLE(','))
			cpp_error(this,"Expecting comma in macro definition.");
		      SKIPWHITE();
		    }
		  tmp2=pos;

		  if(!isidchar(data[pos]))
		  {
		    cpp_error(this,"Expected argument for macro.");
		    break;
		  }

		  while(isidchar(data[pos])) pos++;
		  check_stack(1);
		  push_string(make_shared_binary_string(data+tmp2, pos-tmp2));

		  SKIPWHITE();
		  argno++;
		  if(argno>=MAX_ARGS)
		  {
		    cpp_error(this,"Too many arguments in macro definition.");
		    pop_stack();
		    argno--;
		  }
		}

	      if(!GOBBLE(')'))
		cpp_error(this,"Missing ) in macro definition.");
	    }

	  SKIPSPACE();

	  partbase=sp;
	  init_string_builder(&str, 0);
	  
	  while(1)
	  {
	    INT32 extra=0;

/*	    fprintf(stderr,"%c",data[pos]);
	    fflush(stderr); */

	    switch(data[pos++])
	    {
	    case '/':
	      if(data[pos]=='/')
	      {
		string_builder_putchar(&str, ' ');
		FIND_EOL();
		continue;
	      }
	      
	      if(data[pos]=='*')
	      {
		PUTC(' ');
		SKIPCOMMENT();
		continue;
	      }
	      
	      string_builder_putchar(&str, '/');
	      continue;
	      
	    case '0': case '1': case '2': case '3':	case '4':
	    case '5': case '6': case '7': case '8':	case '9':
	      string_builder_putchar(&str, ((unsigned char *)data)[pos-1]);
	      while(data[pos]>='0' && data[pos]<='9')
		string_builder_putchar(&str, ((unsigned char *)data)[pos++]);
	      continue;
	      
	    case '#':
	      if(GOBBLE('#'))
	      {
		extra=DEF_ARG_NOPRESPACE;
		while(str.s->len && isspace(((unsigned char *)str.s->str)[str.s->len-1]))
		  str.s->len--;
		if(!str.s->len && sp-partbase>1)
		{
#ifdef PIKE_DEBUG
		  if(sp[-1].type != T_INT)
		    fatal("Internal error in CPP\n");
#endif
		  sp[-1].u.integer|=DEF_ARG_NOPOSTSPACE;
		}
	      }else{
		extra=DEF_ARG_STRINGIFY;
	      }
	      SKIPSPACE();
	      pos++;
	      /* fall through */
	      
	    default:
	      if(isidchar(data[pos-1]))
	      {
		struct pike_string *s;
		tmp3=pos-1;
		while(isidchar(data[pos])) pos++;
		if(argno>0)
		{
		  if((s=binary_findstring(data+tmp3,pos-tmp3)))
		  {
		    for(e=0;e<argno;e++)
		    {
		      if(argbase[e].u.string == s)
		      {
			check_stack(2);
			push_string(finish_string_builder(&str));
			init_string_builder(&str, 0);
			push_int(e | extra);
			extra=0;
			break;
		      }
		    }
		    if(e!=argno) continue;
		  }
		}
		string_builder_binary_strcat(&str, data+tmp3, pos-tmp3);
	      }else{
		string_builder_putchar(&str, ((unsigned char *)data)[pos-1]);
	      }
	      extra=0;
	      continue;
	      
	    case '"':
	      FIXSTRING(str, 1);
	      continue;
	      
	    case '\'':
	      tmp3=pos-1;
	      FIND_END_OF_CHAR();
	      string_builder_binary_strcat(&str, data+tmp3, pos-tmp3);
	      continue;
	      
	    case '\\':
	      if(GOBBLE('\n'))
	      { 
		this->current_line++;
		PUTNL();
	      }
	      continue;
	      
	    case '\n':
		PUTNL();
		this->current_line++;
	    case 0:
		break;
	    }
	    push_string(finish_string_builder(&str));
	    break;
	  }

	  if(OUTP())
	  {
	    def=alloc_empty_define(make_shared_binary_string(data+namestart,
						  nameend-namestart),
				   (sp-partbase)/2);
	    copy_shared_string(def->first, partbase->u.string);
	    def->args=argno;
	    
	    for(e=0;e<def->num_parts;e++)
	    {
#if 1
	      if(partbase[e*2+1].type != T_INT)
		fatal("Cpp internal error, expected integer!\n");
	      
	      if(partbase[e*2+2].type != T_STRING)
		fatal("Cpp internal error, expected string!\n");
#endif
	      def->parts[e].argument=partbase[e*2+1].u.integer;
	      copy_shared_string(def->parts[e].postfix,
				 partbase[e*2+2].u.string);
	    }
	    
#ifdef PIKE_DEBUG
	    if(def->num_parts==1 &&
	       (def->parts[0].argument & DEF_ARG_MASK) > MAX_ARGS)
	      fatal("Internal error in define\n");
#endif	  
	    
	    this->defines=hash_insert(this->defines, & def->link);
	    
	  }
	  pop_n_elems(sp-argbase);
	  break;
	}
      
      goto unknown_preprocessor_directive;
      
    case 'u': /* undefine */
      if(WGOBBLE("undefine") || WGOBBLE("undef"))
	{
	  INT32 tmp;
	  struct pike_string *s;

	  SKIPSPACE();

	  tmp=pos;
	  if(!isidchar(data[pos]))
	    cpp_error(this,"Undefine what?");

	  while(isidchar(data[pos])) pos++;

	  /* #undef some_long_identifier
	   *        ^                   ^
	   *        tmp               pos
	   */

	  if(OUTP())
	  {
	    if((s=binary_findstring(data+tmp, pos-tmp)))
	      undefine(this,s);
	  }

	  break;
	}

      goto unknown_preprocessor_directive;

    case 'p': /* pragma */
      if(WGOBBLE("pragma"))
	{
	  if(OUTP())
	    STRCAT("#pragma", 7);
	  else
	    FIND_EOL();
	  break;
	}

    default:
    unknown_preprocessor_directive:
      {
      char buffer[180];
      int i;
      for(i=0;i<(long)sizeof(buffer)-1;i++)
      {
	if(!isidchar(data[pos])) break;
	buffer[i]=data[pos++];
      }
      buffer[i]=0;
	
      cpp_error(this,"Unknown preprocessor directive.");
      }
    }
    }
  }

  if(flags & CPP_EXPECT_ENDIF)
    error("End of file while searching for #endif\n");

  return pos;
}

static INT32 calcC(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  FINDTOK();

/*  DUMPPOS("calcC"); */

  switch(data[pos])
  {
  case '(':
    pos=calc1(this,data,len,pos+1);
    FINDTOK();
    if(!GOBBLE(')'))
      error("Missing ')'\n");
    break;
    
  case '0':
    if(data[pos+1]=='x' || data[pos+1]=='X')
    {
      char *p;
      push_int(STRTOL(data+pos+2, &p, 16));
      pos=p-data;
      break;
    }
    
  case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
  {
    char *p1,*p2;
    double f;
    long l;
    
    f=my_strtod(data+pos, &p1);
    l=STRTOL(data+pos, &p2, 0);
    if(p1 > p2)
    {
      push_float(f);
      pos=p1-data;
    }else{
      push_int(l);
      pos=p2-data;
    }
    break;
  }

  case '\'':
  {
    int tmp;
    READCHAR(tmp);
    pos++;
    if(!GOBBLE('\''))
      error("Missing end quote in character constant.\n");
    push_int(tmp);
    break;
  }

  case '"':
  {
    struct string_builder s;
    init_string_builder(&s, 0);
    READSTRING(s);
    push_string(finish_string_builder(&s));
    break;
  }
  
  default:
#ifdef PIKE_DEBUG
    if(isidchar(data[pos]))
      error("Syntax error in #if (should not happen)\n");
#endif

    error("Syntax error in #if.\n");
  }
  

  FINDTOK();

  while(GOBBLE('['))
  {
    pos=calc1(this,data,len,pos);
    f_index(2);

    FINDTOK();
    if(!GOBBLE(']'))
      error("Missing ']'");
  }
/*   DUMPPOS("after calcC"); */
  return pos;
}

static INT32 calcB(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  FINDTOK();
  switch(data[pos])
  {
    case '-': pos++; pos=calcB(this,data,len,pos); o_negate(); break;
    case '!': pos++; pos=calcB(this,data,len,pos); o_not(); break;
    case '~': pos++; pos=calcB(this,data,len,pos); o_compl(); break;
    default: pos=calcC(this,data,len,pos);
  }
/*   DUMPPOS("after calcB"); */
  return pos;
}

static INT32 calcA(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calcB(this,data,len,pos);
  while(1)
  {
/*     DUMPPOS("inside calcA"); */
    FINDTOK();
    switch(data[pos])
    {
      case '/':
	if(data[1]=='/' || data[1]=='*') return pos;
	pos++;
	pos=calcB(this,data,len,pos);
	o_divide();
	continue;

      case '*':
	pos++;
	pos=calcB(this,data,len,pos);
	o_multiply();
	continue;

      case '%':
	pos++;
	pos=calcB(this,data,len,pos);
	o_mod();
	continue;
    }
    break;
  }
/*   DUMPPOS("after calcA"); */
  return pos;
}

static INT32 calc9(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calcA(this,data,len,pos);

  while(1)
  {
    FINDTOK();
    switch(data[pos])
    {
      case '+':
	pos++;
	pos=calcA(this,data,len,pos);
	f_add(2);
	continue;

      case '-':
	pos++;
	pos=calcA(this,data,len,pos);
	o_subtract();
	continue;
    }
    break;
  }

/*   DUMPPOS("after calc9"); */
  return pos;
}

static INT32 calc8(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc9(this,data,len,pos);

  while(1)
  {
    FINDTOK();
    if(GOBBLEOP("<<"))
    {
      pos=calc9(this,data,len,pos);
      o_lsh();
      break;
    }

    if(GOBBLEOP(">>"))
    {
      pos=calc9(this,data,len,pos);
      o_rsh();
      break;
    }

    break;
  }
  return pos;
}

static INT32 calc7b(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc8(this,data,len,pos);

  while(1)
  {
    FINDTOK();
    
    switch(data[pos])
    {
      case '<':
	if(data[pos]+1 == '<') break;
	pos++;
	if(GOBBLE('='))
	{
	   pos=calc8(this,data,len,pos);
	   f_le(2);
	}else{
	   pos=calc8(this,data,len,pos);
	   f_lt(2);
	}
	continue;

      case '>':
	if(data[pos]+1 == '>') break;
	pos++;
	if(GOBBLE('='))
	{
	   pos=calc8(this,data,len,pos);
	   f_ge(2);
	}else{
	   pos=calc8(this,data,len,pos);
	   f_gt(2);
	}
	continue;
    }
    break;
  }
  return pos;
}

static INT32 calc7(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc7b(this,data,len,pos);

  while(1)
  {
    FINDTOK();
    if(GOBBLEOP("=="))
    {
      pos=calc7b(this,data,len,pos);
      f_eq(2);
      continue;
    }

    if(GOBBLEOP("!="))
    {
      pos=calc7b(this,data,len,pos);
      f_ne(2);
      continue;
    }

    break;
  }
  return pos;
}

static INT32 calc6(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc7(this,data,len,pos);

  FINDTOK();
  while(data[pos] == '&' && data[pos+1]!='&')
  {
    pos++;
    pos=calc7(this,data,len,pos);
    o_and();
  }
  return pos;
}

static INT32 calc5(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc6(this,data,len,pos);

  FINDTOK();
  while(GOBBLE('^'))
  {
    pos=calc6(this,data,len,pos);
    o_xor();
  }
  return pos;
}

static INT32 calc4(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc5(this,data,len,pos);

  FINDTOK();
  while(data[pos] == '|' && data[pos+1]!='|')
  {
    pos++;
    pos=calc5(this,data,len,pos);
    o_or();
  }
  return pos;
}

static INT32 calc3(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc4(this,data,len,pos);

  FINDTOK();
  while(GOBBLEOP("&&"))
  {
    check_destructed(sp-1);
    if(IS_ZERO(sp-1))
    {
      pos=calc4(this,data,len,pos);
      pop_stack();
    }else{
      pop_stack();
      pos=calc4(this,data,len,pos);
    }
  }
  return pos;
}

static INT32 calc2(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc3(this,data,len,pos);

  FINDTOK();
  while(GOBBLEOP("||"))
  {
    check_destructed(sp-1);
    if(!IS_ZERO(sp-1))
    {
      pos=calc3(this,data,len,pos);
      pop_stack();
    }else{
      pop_stack();
      pos=calc3(this,data,len,pos);
    }
  }
  return pos;
}

static INT32 calc1(struct cpp *this,char *data,INT32 len,INT32 pos)
{
  pos=calc2(this,data,len,pos);

  FINDTOK();

  if(GOBBLE('?'))
  {
    pos=calc1(this,data,len,pos);
    if(!GOBBLE(':'))
      error("Colon expected");
    pos=calc1(this,data,len,pos);

    check_destructed(sp-3);
    assign_svalue(sp-3,IS_ZERO(sp-3)?sp-1:sp-2);
    pop_n_elems(2);
  }
  return pos;
}

static int calc(struct cpp *this,char *data,INT32 len,INT32 tmp)
{
  JMP_BUF recovery;
  INT32 pos;

/*  fprintf(stderr,"Calculating\n"); */

  if (SETJMP(recovery))
  {
    pos=tmp;
    if(throw_value.type == T_ARRAY && throw_value.u.array->size)
    {
      union anything *a;
      a=low_array_get_item_ptr(throw_value.u.array, 0, T_STRING);
      if(a)
      {
	cpp_error(this,a->string->str);
      }else{
	cpp_error(this,"Nonstandard error format.");
      }
    }else{
      cpp_error(this,"Nonstandard error format.");
    }
    FIND_EOL();
    push_int(0);
  }else{
    pos=calc1(this,data,len,tmp);
    check_destructed(sp-1);
  }
  UNSETJMP(recovery);

/*  fprintf(stderr,"Done\n"); */

  return pos;
}

void free_one_define(struct hash_entry *h)
{
  int e;
  struct define *d=BASEOF(h, define, link);

  for(e=0;e<d->num_parts;e++)
    free_string(d->parts[e].postfix);
  if(d->first)
    free_string(d->first);
  free((char *)d);
}

/*** Magic defines ***/
static void insert_current_line(struct cpp *this,
				struct define *def,
				struct define_argument *args,
				struct string_builder *tmp)
{
  char buf[20];
  sprintf(buf," %ld ",(long)this->current_line);
  string_builder_binary_strcat(tmp, buf, strlen(buf));
}

static void insert_current_file_as_string(struct cpp *this,
					  struct define *def,
					  struct define_argument *args,
					  struct string_builder *tmp)
{
  PUSH_STRING(this->current_file->str, this->current_file->len, tmp);
}

static void insert_current_time_as_string(struct cpp *this,
					  struct define *def,
					  struct define_argument *args,
					  struct string_builder *tmp)
{
  time_t tmp2;
  char *buf;
  time(&tmp2);
  buf=ctime(&tmp2);

  PUSH_STRING(buf+11, 8, tmp);
}

static void insert_current_date_as_string(struct cpp *this,
					  struct define *def,
					  struct define_argument *args,
					  struct string_builder *tmp)
{
  time_t tmp2;
  char *buf;
  time(&tmp2);
  buf=ctime(&tmp2);

  PUSH_STRING(buf+19, 5, tmp);
  PUSH_STRING(buf+4, 6, tmp);
}

static void check_defined(struct cpp *this,
			  struct define *def,
			  struct define_argument *args,
			  struct string_builder *tmp)
{
  struct pike_string *s;
  s=binary_findstring(args[0].arg, args[0].len);
  if(s && find_define(s))
  {
    string_builder_binary_strcat(tmp, " 1 ", 3);
  }else{
    string_builder_binary_strcat(tmp, " 0 ", 3);
  }
}

static void dumpdef(struct cpp *this,
		    struct define *def,
		    struct define_argument *args,
		    struct string_builder *tmp)
{
  struct pike_string *s;
  struct define *d;
  s=binary_findstring(args[0].arg, args[0].len);
  if(s && (d=find_define(s)))
  {
    int e;
    char buffer[42];
    PUSH_STRING(d->link.s->str,d->link.s->len, tmp);
    if(d->magic)
    {
      string_builder_binary_strcat(tmp, " defined magically ", 19);
    }else{
      string_builder_binary_strcat(tmp, " defined as ", 12);
      
      if(d->first)
	PUSH_STRING(d->first->str, d->first->len, tmp);
      for(e=0;e<d->num_parts;e++)
      {
	if(!(d->parts[e].argument & DEF_ARG_NOPRESPACE))
	  string_builder_putchar(tmp, ' ');
	
	if(d->parts[e].argument & DEF_ARG_STRINGIFY)
	  string_builder_putchar(tmp, '#');
	
	sprintf(buffer,"%ld",(long)(d->parts[e].argument & DEF_ARG_MASK));
	string_builder_binary_strcat(tmp, buffer, strlen(buffer));
	
	if(!(d->parts[e].argument & DEF_ARG_NOPOSTSPACE))
	  string_builder_putchar(tmp, ' ');
	
	PUSH_STRING(d->parts[e].postfix->str, d->parts[e].postfix->len, tmp);
      } 
    }
  }else{
    string_builder_binary_strcat(tmp, " 0 ", 3);
  }
}

static int do_safe_index_call(struct pike_string *s);

static void check_constant(struct cpp *this,
			  struct define *def,
			  struct define_argument *args,
			  struct string_builder *tmp)
{
  struct svalue *save_stack=sp;
  struct svalue *sv;
  char *data=args[0].arg;
  int res,dlen,len=args[0].len;

  while(len && isspace(((unsigned char *)data)[0])) { data++; len--; }

  if(!len)
    cpp_error(this,"#if constant() with empty argument.\n");

  for(dlen=0;dlen<len;dlen++)
    if(!isidchar(data[dlen]))
      break;

  push_string(make_shared_binary_string(data, dlen));
  if((sv=low_mapping_string_lookup(get_builtin_constants(),
				   sp[-1].u.string)))
  {
    pop_stack();
    push_svalue(sv);
    res=1;
  }else if(get_master()) {
    ref_push_string(this->current_file);
    SAFE_APPLY_MASTER("resolv",2);

    res=(throw_value.type!=T_STRING) &&
      (!(IS_ZERO(sp-1) && sp[-1].subtype == NUMBER_UNDEFINED));
  }else{
    res=0;
  }

  while(1)
  {
    data+=dlen;
    len-=dlen;
  
    while(len && isspace(((unsigned char *)data)[0])) { data++; len--; }

    if(!len) break;

    if(data[0]=='.')
    {
      data++;
      len--;
      
      while(len && isspace(((unsigned char *)data)[0])) { data++; len--; }

      for(dlen=0;dlen<len;dlen++)
	if(!isidchar(data[dlen]))
	  break;

      if(res)
      {
        struct pike_string *s=make_shared_binary_string(data, dlen);
	res=do_safe_index_call(s);
        free_string(s);
      }
    }else{
      cpp_error(this, "Garbage characters in constant()\n");
    }
  }

  pop_n_elems(sp-save_stack);

  string_builder_binary_strcat(tmp, res?" 1 ":" 0 ", 3);
}


static int do_safe_index_call(struct pike_string *s)
{
  int res;
  JMP_BUF recovery;
  if(!s) return 0;
  
  if (SETJMP(recovery)) {
    res = 0;
  } else {
    ref_push_string(s);
    f_index(2);
    
    res=!(IS_ZERO(sp-1) && sp[-1].subtype == NUMBER_UNDEFINED);
  }
  UNSETJMP(recovery);
  return res;
}

static struct pike_string *recode_string(struct pike_string *data)
{
  fprintf(stderr, "recode_string()\n");

  /* Observations:
   *
   * * At least a prefix of two bytes need to be 7bit in a valid
   *   Pike program.
   *
   * * NUL isn't valid in a Pike program.
   */
  /* Heuristic:
   *
   * Index 0 | Index 1 | Interpretation
   * --------+---------+------------------------------------------
   *       0 |       0 | 32bit wide string.
   *       0 |      >0 | 16bit Unicode string.
   *      >0 |       0 | 16bit Unicode string reverse byte order.
   *    0xfe |    0xff | 16bit Unicode string.
   *    0xff |    0xfe | 16bit Unicode string reverse byte order.
   *    0x7b |    0x83 | EBCDIC-US ("#c").
   *    0x7b |    0x40 | EBCDIC-US ("# ").
   *    0x7b |    0x09 | EBCDIC-US ("#\t").
   * --------+---------+------------------------------------------
   *   Other |   Other | 8bit standard string.
   *
   * Note that the tests below are more lenient than the table above.
   * This shouldn't matter, since the other cases would be erroneus
   * anyway.
   */

  /* Add an extra reference to data, since we may return it as is. */
  add_ref(data);

  if ((!((unsigned char *)data->str)[0]) ||
      (((unsigned char *)data->str)[0] == 0xfe) ||
      (((unsigned char *)data->str)[0] == 0xff) ||
      (!((unsigned char *)data->str)[1])) {
    /* Unicode */
    if ((!((unsigned char *)data->str)[0]) &&
	(!((unsigned char *)data->str)[1])) {
      /* 32bit Unicode (UCS4) */
    } else {
      /* 16bit Unicode (UCS2) */
      if ((!((unsigned char *)data->str)[1]) ||
	  (((unsigned char *)data->str)[1] == 0xfe)) {
	/* Reverse Byte-order */
	struct pike_string new_str = begin_shared_string(data->len);
	int i;
	for(i=0; i<data->len; i++) {
	  new_str->str[i^1] = data->str[i];
	}
	new_str = end_shared_string(new_str);
	free_string(data);
	data = new_str;
      }
      /* Note: We lose the extra reference to data here. */
      push_string(data);
      f_unicode_to_string(1);
      add_ref(data = sp[-1].u.string);
      pop_stack();
    }
  } else if (((unsigned char *)data->str)[0] == 0x7b) {
    /* EBCDIC */
    /* Notes on EBCDIC:
     *
     * * EBCDIC conversion needs to first convert the first line
     *   according to EBCDIC-US, and then the rest of the string
     *   according to the encoding specified by the first line.
     *
     * * It's an error for a program written in EBCDIC not to
     *   start with a #charset directive.
     *
     * Obfuscation note:
     *
     * * This still allows the rest of the file to be written in
     *   another encoding than EBCDIC.
     */
  }
  return data;
}

static struct pike_string *filter_bom(struct pike_string *data)
{
  /* More notes:
   *
   * * Character 0xfeff (ZERO WIDTH NO-BREAK SPACE = BYTE ORDER MARK = BOM)
   *   needs to be filtered away before processing continues.
   */
  int i;
  int j = 0;
  int len = data->len;
  struct string_builder buf;

  fprintf(stderr, "filter_bom()\n");

  /* Add an extra reference to data here, since we may return it as is. */
  add_ref(data);

  if (!data->size_shift) {
    return(data);
  }
  
  init_string_builder(&buf, data->size_shift);
  if (data->size_shift == 1) {
    /* 16 bit string */
    p_wchar1 *ptr = STR1(data);
    for(i = 0; i<len; i++) {
      if (ptr[i] == 0xfeff) {
	if (i != j) {
	  string_builder_append(&buf, MKPCHARP(ptr + j, 1), i - j);
	  j = i+1;
	}
      }
    }
    if ((j) && (i != j)) {
      /* Add the trailing string */
      string_builder_append(&buf, MKPCHARP(ptr + j, 1), i - j);
      free_string(data);
      data = finish_string_builder(&buf);
    } else {
      /* String didn't contain 0xfeff */
      free_string_builder(&buf);
    }
  } else {
    /* 32 bit string */
    p_wchar2 *ptr = STR2(data);
    for(i = 0; i<len; i++) {
      if (ptr[i] == 0xfeff) {
	if (i != j) {
	  string_builder_append(&buf, MKPCHARP(ptr + j, 2), i - j);
	  j = i+1;
	}
      }
    }
    if ((j) && (i != j)) {
      /* Add the trailing string */
      string_builder_append(&buf, MKPCHARP(ptr + j, 2), i - j);
      free_string(data);
      data = finish_string_builder(&buf);
    } else {
      /* String didn't contain 0xfeff */
      free_string_builder(&buf);
    }
  }
  return(data);
}

void f_cpp(INT32 args)
{
  struct pike_string *data;
  struct pike_predef_s *tmpf;
  struct svalue *save_sp = sp - args;
  struct cpp this;
  int auto_convert = 0;

  if(args<1)
    error("Too few arguments to cpp()\n");

  if(sp[-args].type != T_STRING)
    error("Bad argument 1 to cpp()\n");

  if(args>1)
  {
    if(sp[1-args].type != T_STRING)
      error("Bad argument 2 to cpp()\n");
    copy_shared_string(this.current_file, sp[1-args].u.string);

    if (args > 2) {
      if (sp[2-args].type != T_INT) {
	error("Bad argument 3 to cpp()\n");
      }
      auto_convert = sp[2-args].u.integer;
    }
  }else{
    this.current_file=make_shared_string("-");
  }

  add_ref(data = sp[-args].u.string);

  if (auto_convert && (!data->size_shift) && (data->len > 1)) {
    /* Try to determine if we need to recode the string */
    struct pike_string *new_data = recode_string(data);
    free_string(data);
    data = new_data;
  }
  if (data->size_shift) {
    /* Get rid of any byte order marks (0xfeff) */
    struct pike_string *new_data = filter_bom(data);
    free_string(data);
    data = new_data;
  }

  init_string_builder(&this.buf, 0);
  this.current_line=1;
  this.compile_errors=0;
  this.defines=0;
  do_magic_define(&this,"__LINE__",insert_current_line);
  do_magic_define(&this,"__FILE__",insert_current_file_as_string);
  do_magic_define(&this,"__DATE__",insert_current_date_as_string);
  do_magic_define(&this,"__TIME__",insert_current_time_as_string);

  {
    struct define* def=alloc_empty_define(make_shared_string("__dumpdef"),0);
    def->magic=dumpdef;
    def->args=1;
    this.defines=hash_insert(this.defines, & def->link);
  }

  simple_add_define(&this,"__PIKE__"," 1 ");
  simple_add_define(&this,"__VERSION__"," 0.7 ");
#ifdef __NT__
  simple_add_define(&this,"__NT__"," 1 ");
#endif
#ifdef __amigaos__
  simple_add_define(&this,"__amigaos__"," 1 ");
#endif

  for (tmpf=pike_predefs; tmpf; tmpf=tmpf->next)
    simple_add_define(&this, tmpf->name, tmpf->value);

  string_builder_binary_strcat(&this.buf, "# 1 ", 4);
  PUSH_STRING(this.current_file->str, this.current_file->len, &this.buf);
  string_builder_putchar(&this.buf, '\n');

  low_cpp(&this, data->str, data->len, 0);
  if(this.defines)
    free_hashtable(this.defines, free_one_define);

  free_string(this.current_file);

  free_string(data);

  if(this.compile_errors)
  {
    free_string_builder(&this.buf);
    error("Cpp() failed\n");
  }else{
    pop_n_elems(sp - save_sp);
    push_string(finish_string_builder(&this.buf));
  }
}

void init_cpp()
{
  defined_macro=alloc_empty_define(make_shared_string("defined"),0);
  defined_macro->magic=check_defined;
  defined_macro->args=1;

  constant_macro=alloc_empty_define(make_shared_string("constant"),0);
  constant_macro->magic=check_constant;
  constant_macro->args=1;

  
/* function(string,string|void,int|void:string) */
  ADD_EFUN("cpp",f_cpp,tFunc(tStr tOr(tStr,tVoid) tOr(tInt,tVoid),tStr),OPT_EXTERNAL_DEPEND);
}


void add_predefine(char *s)
{
  struct pike_predef_s *tmp=ALLOC_STRUCT(pike_predef_s);
  char * pos=STRCHR(s,'=');
  if(pos)
  {
    tmp->name=(char *)xalloc(pos-s+1);
    MEMCPY(tmp->name,s,pos-s);
    tmp->name[pos-s]=0;

    tmp->value=(char *)xalloc(s+strlen(s)-pos);
    MEMCPY(tmp->value,pos+1,s+strlen(s)-pos);
  }else{
    tmp->name=(char *)xalloc(strlen(s)+1);
    MEMCPY(tmp->name,s,strlen(s)+1);

    tmp->value=(char *)xalloc(4);
    MEMCPY(tmp->value," 1 ",4);
  }
  tmp->next=pike_predefs;
  pike_predefs=tmp;
}

void exit_cpp()
{
  struct pike_predef_s *tmp;
  while((tmp=pike_predefs))
  {
    free(tmp->name);
    free(tmp->value);
    pike_predefs=tmp->next;
    free((char *)tmp);
  }
  free_string(defined_macro->link.s);
  free((char *)defined_macro);

  free_string(constant_macro->link.s);
  free((char *)constant_macro);
}
