/*
 *			V I E W R A N G E
 *
 *  RT-View-Module for visualizing range data.  The output is a
 *  UNIX-Plot file.  Direction vectors are preserved so that
 *  perspective is theoretically possible.
 *  The algorithm is based on plotting all the hit distances for all
 *  the pixels ray-traced.
 *
 *  Author -
 *	Susanne L. Muuss, J.D.
 *  
 *  Source -
 *	SECAD/VLD Computing Consortium, Bldg 394
 *	The U. S. Army Ballistic Research Laboratory
 *	Aberdeen Proving Ground, Maryland  21005
 *  
 *  Copyright Notice -
 *	This software is Copyright (C) 1991 by the United States Army.
 *	All rights reserved.
 */


#ifndef lint
static char RCSrayrange[] = "@(#)$Header$";
#endif

#include <stdio.h>
#include <math.h>

#include "machine.h"
#include "vmath.h"
#include "raytrace.h"
#include "./material.h"
#include "./ext.h"
#include "rdebug.h"

#define CELLNULL ( (struct cell *) 0)

struct cell {
	float	c_dist;			/* distance from emanation plane to in_hit */
	point_t	c_hit;			/* 3-space hit point of ray */
};

extern	int	width;			/* # of pixels in X; picture width */
extern int	npsw;			/* number of worker PSWs to run */
float		max_dist;
struct cell	*cellp;

int		use_air = 0;		/* Internal air recognition is off */

int		using_mlib = 0;		/* Material routines NOT used */

/* Viewing module specific "set" variables */
struct structparse view_parse[] = {
	{"",	0, (char *)0,	0,	FUNC_NULL }
};


char usage[] = "\
Usage:  rtrange [options] model.g objects... >file.ray\n\
Options:\n\
 -s #		Grid size in pixels, default 512\n\
 -a Az		Azimuth in degrees	(conflicts with -M)\n\
 -e Elev	Elevation in degrees	(conflicts with -M)\n\
 -M		Read model2view matrix on stdin (conflicts with -a, -e)\n\
 -o model.g	Specify output file (default=stdout)\n\
 -U #		Set use_air boolean to # (default=1)\n\
 -x #		Set librt debug flags\n\
";

int	rayhit(), raymiss();

/*
 *  			V I E W _ I N I T
 *
 *  This routine is called by main().  It initializes the entire run, i.e.,
 *  it does things such as opening files, etc., which must be done before
 *  any other computations take place.  It is called only once per run.
 *  Pointers to rayhit() and raymiss() are set up and are later called from
 *  do_run().
 */

int
view_init( ap, file, obj, minus_o )
register struct application *ap;
char *file, *obj;
{

	ap->a_hit = rayhit;
	ap->a_miss = raymiss;
	ap->a_onehit = 1;

	output_is_binary = 1;		/* output is binary */

	return(0);			/* No framebuffer needed */
}

/*
 *			V I E W _ 2 I N I T
 *
 *  A null-function.
 *  View_2init is called by do_frame(), which in turn is called by
 *  main() in rt.c.  This routine is called once per frame.  Static
 *  images only have one frame.  Animations have MANY frames, and bounding
 *  boxes, for example, need to be computed once per frame.
 *  Never preclude a new and nifty animation: rule: if it's a variable, it can
 *  change from frame to frame ( frame/picture width; angle between surface
 *  normals triggering shading.... etc).
 */

void
view_2init( ap )
struct application	*ap;
{
	if( outfp == NULL )
		rt_bomb("outfp is NULL\n");

	/*
	 *  For now, RTHIDE does not operate in parallel, while ray-tracing.
	 *  However, not dropping out of parallel mode until here permits
	 *  tree walking and database prepping to still be done in parallel.
	 */
	if( npsw >= 1 )  {
		rt_log("Note: changing from %d cpus to 1 cpu\n", npsw );
		npsw = 1;		/* Disable parallel processing */
	}


	/* malloc() a buffer that has room for as many struct cell 's
	 * as the incoming file is wide (width).
	 * Rather than using malloc(), though, rt_malloc() is used.  This
	 * has the advantage of inbuild error-checking and automatic aborting
	 * if there is no memory.  Also, rt_malloc() takes a string as its
	 * final parameter: this tells the usr exactly where memory ran out.
	 */


	cellp = (struct cell *)rt_malloc(sizeof(struct cell) * width,
		"cell buffer" );


	/* Obtain the maximun distance within the model to use as the
	 * background distance.  Also get the coordinates of the model's
	 * bounding box and feed them to 
	 * pdv_3space.  This will allow the image to appear in the plot
	 * starting with the same size as the model.
	 */

	pdv_3space(outfp, ap->a_rt_i->rti_pmin, ap->a_rt_i->rti_pmax);

	/* Find the max dist fron emantion plane to end of model
	 * space.  This can be twice the radius of the bounding
	 * sphere.
	 */

	max_dist = 2 * (ap->a_rt_i->rti_radius);
}


/*
 *			R A Y M I S S
 *
 *  This function is called by rt_shootray(), which is called by
 *  do_frame(). Records coordinates where a miss is detected.
 */

int
raymiss( ap )
register struct application	*ap;
{

	struct	cell	*posp;		/* store the current cell position */

	/* Getting defensive.... just in case. */
	if(ap->a_x > width)  {
		rt_bomb("raymiss: pixels exceed width\n");
	}

	posp = &(cellp[ap->a_x]);

	/* Find the hit point for the miss. */

	VJOIN1(posp->c_hit, ap->a_ray.r_pt, max_dist, ap->a_ray.r_dir);
	posp->c_dist = max_dist;

	return(0);
}

/*
 *			V I E W _ P I X E L
 *
 *  This routine is called from do_run(), and in this case does nothing.
 */

void
view_pixel()
{
	return;
}

void view_setup() {}
void view_cleanup() {}


/*
 *			R A Y H I T
 *
 *  Rayhit() is called by rt_shootray() when a hit is detected.  It
 *  computes the hit distance, the distance traveled by the
 *  ray, and the direction vector.
 *  
 */

int
rayhit( ap, PartHeadp )
struct application *ap;
register struct partition *PartHeadp;
{
	register struct partition *pp = PartHeadp->pt_forw;
	struct	cell	*posp;		/* stores current cell position */
	fastf_t		dist;   	/* ray distance */


	if( pp == PartHeadp )
		return(0);		/* nothing was actually hit?? */


	/* Getting defensive.... just in case. */
	if(ap->a_x > width)  {
		rt_bomb("rayhit: pixels exceed width\n");
	}

	posp = &(cellp[ap->a_x]);

	/* Calculate the hit distance and the direction vector.  This is done
	 * by VJOIN1(hitp->hit_point, rp->r_pt, hitp->hit_dist, rp->r_dir).
	 */

	VJOIN1(pp->pt_inhit->hit_point, ap->a_ray.r_pt,
		pp->pt_inhit->hit_dist, ap->a_ray.r_dir);

	/* Now store the distance and the direction vector as appropriate.
	 * Output the ray data: screen plane (pixel) coordinates
	 * for x and y positions of a ray, region_id, and hit_distance.
	 * The x and y positions are represented by ap->a_x and ap->a_y.
	 *
	 *  Assume all rays are parallel.
	 */

	posp->c_dist = pp->pt_inhit->hit_dist;
	VMOVE(posp->c_hit, pp->pt_inhit->hit_point);

	return(0);
}

/*
 *			V I E W _ E O L
 *
 *  View_eol() is called by rt_shootray() in do_run().
 *  This routine is called by worker.c whenever there is a full scanline.
 *  worker.c figures out what is a full scanline.  Whenever there
 *  is a full buffer in memory, the hit distances ar plotted.
 */

void	view_eol(ap)
struct application *ap;

{
	struct cell	*posp;
	int		i;
	int		cont;		/* continue flag */

	posp = &(cellp[0]);
	cont = 0;

	/* Plot the starting point and set cont to 0.  Then
	 * march along the entire array and continue to plot the
	 * hit points based on their distance from the emanation
	 * plane. When consecutive hit-points with identical distances
	 * are found, cont is set to one so that the entire sequence
	 * of like-distanced hit-points can be plotted together.
	 */

	pdv_3move( outfp, posp->c_hit );

	for( i = 0; i < width-1; i++, posp++ )  {
		if( posp->c_dist == (posp+1)->c_dist )  {
			cont = 1;
			continue;
		} else  {
			if(cont)  {
				pdv_3cont(outfp, posp->c_hit);
				cont = 0;
			}
			pdv_3cont(outfp, (posp+1)->c_hit);
		}
	}

	/* Catch the boundary condition if the last couple of cells
	 * heve the same distance.
	 */

	pdv_3cont(outfp, posp->c_hit);
}


/*
 *			V I E W _ E N D
 *
 *  View_end() is called by rt_shootray in do_run().
 */

void
view_end(ap)
struct application *ap;
{

	fflush(outfp);
}


