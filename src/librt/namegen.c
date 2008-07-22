/*                           N A M E G E N . C
 * BRL-CAD
 *
 * Copyright (c) 2008 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @addtogroup librt */
/** @{ */
/** @file namegen.c
 *
 * @brief Implements parser and next-name routines for BRL-CAD object names
 *
 * There are several tools in BRL-CAD which require automatic naming
 * of auto-generated objects, such as clone and mirror.  The routines
 * defined here provide a general and universal method for taking a
 * supplied object name and using it as a basis for generating new
 * names, based on a printf-like syntax specific to BRL-CAD names:
 *
 * n - naming component of the primitive, defined as either the part of the primitive name
 *     from the first character up to the first separator, a non-incremental, non-extension
 *     string between separators in the name, or a non-incremental non-extension string
 *     between a separator and the end of the primitive name.
 * s - a separator between naming elements, one of "-", "_" or ".".  Note that if
 *     one or more of these characters is present but no separator has been specified, the
 *     presence of these characters is not regarded as indicating a separator.
 * i - an incremental section, changed when a new name is to be generated.
 * e - an extension - like %n, but can optionally be assigned based on the type of
 *     object being created.
 * 
 *
 * A formatting string for a primitive name MUST have at least one incremental section. If
 * no separators are specified between a %n and a %i, it is assumed that the first digit
 * character encountered is part of the %i and the previous non-digit character is the final
 * character in that %n or %e region. To have digits present in a %n which is to be followed
 * by a %i it is required to have a separator between the %n region and the %i region.
 */

#include "common.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include "bio.h"
#include "bn.h"
#include "db.h"
#include "raytrace.h"
#include "bu.h"

#define ASSEM_EXT ' '
#define REGION_EXT 'r'
#define COMB_EXT 'c'
#define PRIM_EXT 's'

void count_if_region(struct db_i *dbip, struct directory *dp, genptr_t rcount)
{
    int *counter = (int*)rcount;
    if (dp->d_flags & DIR_REGION) {
	(*counter)++;
    }
}

int determine_object_type(struct db_i *dbip, struct directory *dp) {
	struct resource *resp = &rt_uniresource;
	int rcount = 0;
	int object_type;
	if (!dp) {
		bu_log("Null directory pointer passed to determine_object_type - returning object type 0\n");
		return 0;
	}
	db_functree(dbip, dp, count_if_region, NULL, resp, &rcount);

	if (dp->d_flags & DIR_SOLID) {
		object_type = 1;
	}
	if (dp->d_flags & DIR_REGION) {
		if (rcount > 1) {
			bu_log("WARNING - detected region flag set in subtree of region.  Returning type as region - if assembly type is intended re-structure model to eliminate nested regions.\n");
			object_type = 3;
		} else {
			object_type = 3;
		}
	}
	if ((dp->d_flags & DIR_COMB) && !(dp->d_flags & DIR_REGION)) {
		if (rcount > 0) {
			object_type = 4;
		} else {
			object_type = 2;
		}
	}
	return object_type;
};

struct increment_data {
   struct bu_list l;
   struct bu_vls namestring;
   int numerval;
};

struct object_name_data {
    struct bu_list name_components;
    struct bu_list separators;
    struct bu_list incrementals;
    struct bu_vls extension;
    int object_type; /* 0 = unknown, 1 = solid, 2 = comb, 3 = region, 4 = assembly*/
};

struct object_name_item {
   struct bu_list l;
   struct bu_vls namestring;
};

struct object_name_data *
parse_obj_name(struct db_i *dbip, char *fmt, char *name)
{
    	struct object_name_data *objcomponents;
	BU_GETSTRUCT(objcomponents, object_name_data);
	BU_LIST_INIT(&(objcomponents->name_components));
	BU_LIST_INIT(&(objcomponents->separators));
	BU_LIST_INIT(&(objcomponents->incrementals));
	bu_vls_init(&(objcomponents->extension));

	struct object_name_item *objname;
        struct increment_data *incdata;
	
	struct directory *dp = (struct directory *)NULL;
	struct resource *resp = &rt_uniresource;
	int rcount = 0;

	char *stringholder;
	int ignore_separator_flag = 0;

	int len = 0;
	int i;

	if (!fmt || fmt[0] == '\0' || !name || name[0] == '\0') { 
            bu_log("ERROR: empty name or format string passed to parse_name\n");
	    return;
        }



	dp = db_lookup(dbip, name, LOOKUP_QUIET);
	if (!dp) {
             bu_log("ERROR:  No object named %s found in database.\n", name);
             return;
	}
	

	objcomponents->object_type = determine_object_type(dbip, dp);	
	bu_log("Object type is %d\n",objcomponents->object_type);

  	for (i = 0; i < strlen(fmt); i++) {
	     if (len <= strlen(name)) {
		switch ( fmt[i] ) {
			case 'n':
			{
				BU_GETSTRUCT(objname, object_name_item);
				BU_LIST_INIT(&(objname->l));
				bu_vls_init(&(objname->namestring));
				bu_vls_trunc(&(objname->namestring),0);
				if (fmt[i+1] == 'i' ) {
					while (name[len] != '.' && name[len] != '_' && name[len] != '-' && name[len] != '\0' && !isdigit(name[len])){
						bu_vls_putc(&(objname->namestring), name[len]);
						len++;
					}
				}else {
					while (name[len] != '.' && name[len] != '_' && name[len] != '-' && name[len] != '\0'){
						bu_vls_putc(&(objname->namestring), name[len]);
						len++;
					}
				}
				BU_LIST_INSERT(&(objcomponents->name_components), &(objname->l));
			}
			break;
			case 'i':
			{
				BU_GETSTRUCT(incdata, increment_data);
				BU_LIST_INIT(&(incdata->l));
				bu_vls_init(&(incdata->namestring));
				bu_vls_trunc(&(incdata->namestring),0);
                                if (isdigit(name[len])) {
					while (isdigit(name[len])) {
        	                               bu_vls_putc(&(incdata->namestring), name[len]);
                	                       len++;
					}
				} else {
					bu_log("Note: naming convention requres incrementor here, but no digit found - setting a default value of 1\n");
					bu_vls_putc(&(incdata->namestring), '1');
				}
				incdata->numerval = atoi(bu_vls_addr(&(incdata->namestring)));
				BU_LIST_INSERT(&(objcomponents->incrementals), &(incdata->l));
				
			}
			break;
			case 's':
			{
				if (ignore_separator_flag == 0) {
					BU_GETSTRUCT(objname, object_name_item);
					BU_LIST_INIT(&(objname->l));
					bu_vls_init(&(objname->namestring));
                        	        bu_vls_trunc(&(objname->namestring),0);
					if (name[len] == '.' || name[len] == '_' || name[len] == '-'){
                      	                 bu_vls_putc(&(objname->namestring), name[len]);
                       	                len++;
					} else {
                       	                bu_vls_putc(&(objname->namestring), '.');
					       bu_log("Note: naming convention requires separator but none found at designated point in supplied object name - using '.'\n");
					}
					BU_LIST_INSERT(&(objcomponents->separators), &(objname->l));
				} else {
					ignore_separator_flag = 0;
					len++;
				}
			}
			break;
			case 'e':
			{
				if (objcomponents->object_type == 4  && ASSEM_EXT==' ' && fmt[i+1] == 's') {
					ignore_separator_flag = 1;
					len++;
				} else {
					switch (name[len]) {
						case PRIM_EXT:
						{
							if (objcomponents->object_type == 1) {
								bu_vls_putc(&(objcomponents->extension), name[len]);
							} else {
								bu_vls_putc(&(objcomponents->extension), name[len]);
								bu_log("Warning: primitive found with extension other than %c\n", PRIM_EXT);
							}
						}
						break;
						case COMB_EXT:
						{
							if (objcomponents->object_type == 2) {
                                                                bu_vls_putc(&(objcomponents->extension), name[len]);
                                                        } else {
                                                                bu_vls_putc(&(objcomponents->extension), name[len]);
                                                                bu_log("Warning: combination found with extension other than %c\n", COMB_EXT);
                                                        }
						}
						break;

						case REGION_EXT:
						{
							if (objcomponents->object_type == 3) {
                                                                bu_vls_putc(&(objcomponents->extension), name[len]);
                                                        } else {
                                                                bu_vls_putc(&(objcomponents->extension), name[len]);
                                                                bu_log("Warning: region found with extension other than %c\n", REGION_EXT);
                                                        }
						}
						break;

						case ASSEM_EXT:
						{
							if (objcomponents->object_type == 1) {
                                                                bu_vls_putc(&(objcomponents->extension), name[len]);
                                                        } else {
                                                                bu_vls_putc(&(objcomponents->extension), name[len]);
                                                                bu_log("Warning: assembly found with extension other than %c\n", ASSEM_EXT);
                                                        }
						}
						break;
						default:
						{
							switch (objcomponents->object_type) {
								case 1:
								{
									bu_vls_putc(&(objcomponents->extension), PRIM_EXT);
								}
								break;
								case 2:
								{
									bu_vls_putc(&(objcomponents->extension), COMB_EXT);
								}
								break;
								case 3:
								{
									bu_vls_putc(&(objcomponents->extension), REGION_EXT);
								}
								break;
								case 4:
								{
									bu_vls_putc(&(objcomponents->extension), ASSEM_EXT);
								}
								break;
								default:
								{
									bu_log("Error - invalid object type returned by determine_object_type\n");
								}
								break;	
							}
							bu_log("Note: naming convention requres extension here, but char at designated point is not a valid extension.  Selecting extension char %s based on object type.\n", bu_vls_addr(&(objcomponents->extension)));
						}
					}
					len++;
				}
			}
			break;
			default:
			{
				bu_log("Error - invalid character in object formatting string\n");
			}
			break;
		}
	     }
	}

	return objcomponents;
};		


void test_obj_struct(struct db_i *dbip, char *fmt, char *testname){
	
	struct object_name_item *testitem;
        struct increment_data *inctest;
	struct object_name_data *test;
	test = parse_obj_name(dbip, fmt, testname);
	for ( BU_LIST_FOR( testitem, object_name_item, &(test->name_components) ) )  {
		bu_log("%s ",bu_vls_addr(&(testitem->namestring)));
	}
        bu_log("\n");
	for ( BU_LIST_FOR( testitem, object_name_item, &(test->separators) ) )  {
     		bu_log("%s ",bu_vls_addr(&(testitem->namestring)));
 	};
	bu_log("\n");
	for ( BU_LIST_FOR( inctest, increment_data, &(test->incrementals) ) )  {
	    bu_log("%s, %d ",bu_vls_addr(&(inctest->namestring)),inctest->numerval);
	}

	bu_log("\n");
	bu_log("%s ",bu_vls_addr(&(test->extension)));
	bu_log("\n\n");
	
	for ( BU_LIST_FOR( testitem, object_name_item,  &(test->name_components) ) ) {
		bu_free(testitem, "free names");
	}	

	for ( BU_LIST_FOR( testitem, object_name_item, &(test->separators) ) )  {
		bu_free(testitem, "free separators");
	}
	for ( BU_LIST_FOR( inctest, increment_data, &(test->incrementals) ) )  {
		bu_free(inctest, "free incrementals");
	}
	
	bu_vls_free(&(test->extension));
	bu_free(test, "free name structure");
};

/* Per discussion with Sean - use argc, argv pair for actual requesting, generation and return of names.
 */

/* Have two options here - either supply a pre-parsed object_name_data structure and generate straight from that, or accept a name
 * and parse it out.
 
struct object_name_item *
get_next_name(struct db_i *dbip, char *fmt, char *basename, int *pos_iterator, int *inc_iterator, int argc, char *argv[])
{
	struct bu_vls stringassembly;
	bu_vls_init(&stringassembly);

	struct object_name_data *parsed_name;
	int i;
	int iterator_count = 0;

	parsed_name = parse_obj_name(dbip, fmt, basename);

	bu_vls_trunc(&stringassembly, 0);

	for (i = 0; i < strlen(fmt); i++) {
		switch ( fmt[i] ) {
			case 'n':
			{
				bu_vls_printf(&stringassembly, "%s", BU_LIST_NEXT(&(parsed_name->name_components), &(objname->l)));
			}
			break;
			case 'i':
			{
				iterator_count++;
				if (iterator_count == pos_iterator) {
					countval += inc_iterator;
					bu_vls_printf(&stringassembly, "%d", countval);					
				}
			}
			break;
			case 's':
			{
				if (ignore_separator_flag == 0) {
					bu_vls_printf(&stringassembly, "%s", BU_LIST_NEXT(&(parsed_name->separators), &(objname->l)));
				} else {
					ignore_separator_flag = 0;
				}
			}
			break;
			case 'e':
			{
				if (objtype == assembly && ASSEM_EXT==' ' && fmt[i+1] == 's') {
					ignore_separator_flag = 1;
					len++;
				} else {
					if (name[len] == REGION_EXT || name[len] == COMB_EXT || name[len] == PRIM_EXT) {
						bu_vls_putc(&(objcomponents->extension), name[len]);
						len++;
					} else {
					 add logic to check type of object named by supplied name and use default 
					}
				}
			}
			break;
			default:
			{
				bu_log("Error - invalid character in object formatting string\n");
			}
			break;
		}
	}

}
*/
main(int argc, char **argv)
{
 	struct db_i *dbip;
	dbip = db_open( "./test.g", "r" );
	db_dirbuild(dbip);

	struct bu_vls temp;

	bu_vls_init(&temp);

	bu_vls_trunc(&temp,0);
	bu_vls_printf(&temp,"%s","core-001a.s");
	test_obj_struct(dbip, "nsinse", bu_vls_addr(&temp));
	
	bu_vls_trunc(&temp,0);
	bu_vls_printf(&temp,"%s","s.bcore12.b3");
	test_obj_struct(dbip, "esnisni", bu_vls_addr(&temp));

	bu_vls_trunc(&temp,0);
	bu_vls_printf(&temp,"%s","comb1.c");
	test_obj_struct(dbip, "nise", bu_vls_addr(&temp));

	bu_vls_trunc(&temp,0);
	bu_vls_printf(&temp,"%s","comb2.r");
	test_obj_struct(dbip, "nise", bu_vls_addr(&temp));

	bu_vls_trunc(&temp,0);
	bu_vls_printf(&temp,"%s","comb3.r");
	test_obj_struct(dbip, "nise", bu_vls_addr(&temp));

	bu_vls_trunc(&temp,0);
	bu_vls_printf(&temp,"%s","assem1");
	test_obj_struct(dbip, "ni", bu_vls_addr(&temp));

	bu_vls_trunc(&temp,0);
	bu_vls_printf(&temp,"%s","test.q");
	test_obj_struct(dbip, "nise", bu_vls_addr(&temp));

	db_close(dbip);
};
