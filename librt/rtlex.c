/*		R T L E X . C
 *
 *  Author -
 *	Christopher T. Johnson
 *  
 *  Source -
 *	Geometric Solutions, Inc.
 *  
 *  Distribution Status -
 *	Public Domain, Distribution Unlimited.
 */
#ifndef lint
static char RCSid[] = "@(#)$Header$ (ARL)";
#endif

#include "conf.h"

#include <stdio.h>
#include <ctype.h>
#include "machine.h"
#include "externs.h"
#include "vmath.h"
#include "raytrace.h"
#include "rtstring.h"
#include "rtlex.h"

static int rt_lex_reading_comment = 0;

/*
 *			R T _ G E T O N E
 */
static char *
rt_getone(used, rtstr)
int *used;
struct rt_vls *rtstr;
{
	register char *cp;
	register char *sp;
	register char *unit;
	int	number;

	number = 1;
	*used = 0;

	RT_VLS_CHECK(rtstr);
	cp = rt_vls_addr(rtstr);
top:
	if (rt_lex_reading_comment) {
		for(;;) {
			register char tc;
			tc = *cp; cp++;
			if (!tc) {
				return 0;
			}
			if (tc != '*') continue;
			if (*cp != '/') continue;
			cp++;	/* Skip the '/' */
			break;
		}
		rt_lex_reading_comment = 0;
	}
		
	/*
	 * skip leading blanks
	 */
	for (; *cp && isspace(*cp); cp++);
	/*
	 * Is this a comment?  '#' to end of line is.
	 */
	if (!*cp || *cp == '#') {
		return 0;
	}
	/*
	 * Is this a 'C' multi-line comment?
	 */
	if (*cp == '/' && *(cp+1)=='*') {
		cp += 2;
		rt_lex_reading_comment = 1;
		goto top;
	}
	/*
	 * cp points to the first non-blank character.
	 */
	sp = cp;		/* start pointer */
	while (*cp) {
		register char tc;

		tc = *cp; cp++;
		/*
		 * Numbers come in the following forms
		 *	[0-9]*
		 *	[0-9]*.[0-9][0-9]*
		 *	[0-9]*.[0-9][0-9]*{e|E}{+|-}[0-9][0-9]*
		 */
		if (number) {
			/*
			 * We have not seen anything to make this NOT
			 * a number.
			 */
			if (isdigit(tc)) {
				if (number == 5 || number == 6) number = 7;
				if (number == 3) number = 4;
				if (number == 1) number = 2;
				continue;
			}
			if (number==2 && tc == '.') {
				/*
				 * [0-9][0-9]*.
				 */
				number = 3;
				continue;
			}
			if (number == 4 && (tc == 'e' || tc == 'E')) {
				/*
				 * [0-9][0-9]*.[0-9][0-9]*{e|E}
				 */
				number = 5;
				continue;
			}
			if (number == 5 && (tc == '+' || tc == '-')) {
				/*
				 * [0-9][0-9]*.[0-9][0-9]*{e|E}{+|-}
				 */
				number = 6;
				continue;
			}
			if (number == 3) break;
			number = 0;
		}
		if (!isalnum(tc) && tc != '.' && tc != '_') break;
	}
	if (number ==  6) --cp;	/* subtract off the + or - */
	if (number == 3) --cp;  /* subtract off the . */
	/*
	 * All spaces have been skipped. (sp)
	 * if we had NUMBER. or NUMBERe{+|-} that has be replaced (cp)
	 */
	*used = cp - sp -1;
	if (*used == 0) *used = 1;
	unit = (char *)rt_malloc(*used+1, "unit token");
	strncpy(unit,sp,*used);
	unit[*used] = '\0';
	*used = sp-rt_vls_addr(rtstr) + *used;
	if (*used == 0) *used = 1;
	return unit;
}

/*
 *			R T _ L E X
 */
int
rt_lex(token, rtstr, keywords, symbols)
union rt_lex_token *token;
struct rt_vls *rtstr;
struct rt_lex_key *keywords;
struct rt_lex_key *symbols;
{
	char *unit;
	char *cp;
	int used;

	/*
	 * get a unit of information from rtstr.
	 */
	used = 0;
	unit = rt_getone(&used, rtstr);

	/*
	 * Was line empty or commented out.
	 */
	if (!unit) {
		if (used) rt_bomb("rt_lex: Null unit, and something used.\n");
		return RT_LEX_NEED_MORE;
	}

	/*
	 * Decide if this unit is a symbol, number or identifier.
	 */
	if (isdigit(*unit)) {
		/*
		 * Humm, this could be a number.
		 * 	octal -- 0[0-7]*
		 *	hex   -- 0x[0-9a-f]*
		 *	dec   -- [0-9][0-9]*
		 *	dbl   -- [0-9][0-9]*.[0-9]*{{E|e}{+|-}[0-9][0-9]*}
		 */
		if (*unit == '0') { 	/* any of the above */
			/*
			 * 	octal -- 0[0-7]*
			 */
			for (cp=unit; *cp && *cp>='0' && *cp <='7'; cp++);
			if (!*cp) {	/* We have an octal value */
				token->type = RT_LEX_INT;
				sscanf(unit,"%o",&token->t_int.value);
				rt_free(unit,"unit token");
				return used;
			}
			/*
			 * if it is not an octal number, maybe it is
			 * a hex number?"
			 *	hex   -- 0x[0-9a-f]*
			 */
			cp=unit+1;
			if (*cp == 'x' || *cp == 'X') {
				for(;*cp && isxdigit(*cp);cp++);
				if (!*cp) {
					token->type = RT_LEX_INT;
					sscanf(unit,"%x",&token->t_int.value);
					rt_free(unit, "unit token");
					return used;
				}
			}
		}
		/*
		 * This could be a decimal number, a double or an identifier.
		 *	dec   -- [0-9][0-9]*
		 */
		for (cp=unit; *cp && isdigit(*cp); cp++);
		if (!*cp) {
			token->type = RT_LEX_INT;
			sscanf(unit,"%d", &token->t_int.value);
			rt_free(unit, "unit token");
			return used;
		}
		/*
		 * if we are here, then this is either a double or
		 * an identifier.
		 *	dbl   -- [0-9][0-9]*.[0-9]*{{E|e}{+|-}[0-9][0-9]*}
		 *
		 * *cp should be a '.'
		 */
		if (*cp == '.') {
			for(cp++;*cp &&isdigit(*cp);cp++);
			if (*cp == 'e' || *cp == 'E') cp++;
			if (*cp == '+' || *cp == '-') cp++;
			for(;*cp &&isdigit(*cp);cp++);
			if (!*cp) {
				token->type = RT_LEX_DOUBLE;
				sscanf(unit, "%lg", &token->t_dbl.value);
				rt_free(unit, "unit token");
				return used;
			}
		}
		/*
		 * Oh well, I guess it was not a number.  That means it
		 * must be something else.
		 */
	}
	/*
	 * We either have an identifier, keyword, or symbol.
	 */
	if (symbols) {
		if (!*(unit+1) ) {	/* single character, good choice for a symbol. */
			register struct rt_lex_key *sp;
			for (sp=symbols;sp->tok_val;sp++) {
				if (*sp->string == *unit) {
					token->type = RT_LEX_SYMBOL;
					token->t_key.value = sp->tok_val;
					rt_free(unit, "unit token");
					return used;
				}
			}
		}
	}
	if (keywords) {
		register struct rt_lex_key *kp;
		for (kp=keywords;kp->tok_val; kp++) {
			if (strcmp(kp->string, unit) == 0) {
				token->type = RT_LEX_KEYWORD;
				token->t_key.value = kp->tok_val;
				rt_free(unit, "unit token");
				return used;
			}
		}
	}
	token->type = RT_LEX_IDENT;
	token->t_id.value = unit;
	return used;
}
