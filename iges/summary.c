/*
 *  Authors -
 *	John R. Anderson
 *	Susanne L. Muuss
 *	Earl P. Weaver
 *
 *  Source -
 *	VLD/ASB Building 1065
 *	The U. S. Army Ballistic Research Laboratory
 *	Aberdeen Proving Ground, Maryland  21005
 *  
 *  Copyright Notice -
 *	This software is Copyright (C) 1990 by the United States Army.
 *	All rights reserved.
 */

#include "./iges_struct.h"
#include "./iges_extern.h"

Summary()
{
	int i;
	int indep_entities=0;

	rt_log( "Summary of entity types found:\n" );
	for( i=0 ; i<=ntypes ; i++ )
	{
		if( typecount[i].count != 0 )
			rt_log( "%10d %s (type %d)\n",typecount[i].count , typecount[i].name , typecount[i].type );
	}

	for( i=0 ; i<totentities ; i++ )
	{
		int subord;

		subord = (dir[i]->status/10000)%100;
		if( !subord )
			indep_entities++;
	}
	rt_log( "%d Independent entities\n" , indep_entities );
}
