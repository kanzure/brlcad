/*
 *			C O O K
 *
 *  Notes -
 *	The normals on all surfaces point OUT of the solid.
 *	The incomming light rays point IN.  Thus the sign change.
 *
 *  Authors -
 *	Phillip Dykstra
 *  
 *  Source -
 *	SECAD/VLD Computing Consortium, Bldg 394
 *	The U. S. Army Ballistic Research Laboratory
 *	Aberdeen Proving Ground, Maryland  21005
 *  
 *  Copyright Notice -
 *	This software is Copyright (C) 1985 by the United States Army.
 *	All rights reserved.
 */
#ifndef lint
static char RCScook[] = "@(#)$Header$ (BRL)";
#endif

#include "conf.h"

#include <stdio.h>
#include <math.h>
#include "machine.h"
#include "vmath.h"
#include "mater.h"
#include "raytrace.h"
#include "./rdebug.h"
#include "./material.h"
#include "./mathtab.h"
#include "./light.h"

/* from view.c */
extern double AmbientIntensity;

/* Local information */
struct cook_specific {
	double	m;		/* rms slope - should be a vector of these XXX*/
	int	shine;		/* temporary */
	double	wgt_specular;
	double	wgt_diffuse;
	double	transmit;	/* Moss "transparency" */
	double	reflect;	/* Moss "transmission" */
	double	refrac_index;
	double	extinction;
	double	m2;		/* m^2 - plus check for near zero */
	double	n[3];		/* "effective" RGB refract index */
	double	rd[3];		/* Diffuse reflection coefficient */
};
#define CK_NULL	((struct cook_specific *)0)
#define CL_O(m)	offsetof(struct cook_specific, m)

struct bu_structparse cook_parse[] = {
	{"%f", 1, "m",		CL_O(m),		FUNC_NULL },
	{"%f", 1, "specular",	CL_O(wgt_specular),	FUNC_NULL },
	{"%f", 1, "sp",		CL_O(wgt_specular),	FUNC_NULL },
	{"%f", 1, "diffuse",	CL_O(wgt_diffuse),	FUNC_NULL },
	{"%f", 1, "di",		CL_O(wgt_diffuse),	FUNC_NULL },
	{"%f", 1, "transmit",	CL_O(transmit),		FUNC_NULL },
	{"%f", 1, "tr",		CL_O(transmit),		FUNC_NULL },
	{"%f", 1, "reflect",	CL_O(reflect),		FUNC_NULL },
	{"%f", 1, "re",		CL_O(reflect),		FUNC_NULL },
	{"%f", 1, "ri",		CL_O(refrac_index),	FUNC_NULL },
	{"%f", 1, "extinction",	CL_O(extinction),	FUNC_NULL },
	{"%f", 1, "ex",		CL_O(extinction),	FUNC_NULL },
	{"",   0, (char *)0,	0,			FUNC_NULL }
};

HIDDEN int	cook_setup(), cmirror_setup(), cglass_setup();
HIDDEN int	cook_render();
HIDDEN void	cook_print();
HIDDEN void	cook_free();
HIDDEN double	fresnel();
HIDDEN double	beckmann();

CONST struct mfuncs cook_mfuncs[] = {
	{MF_MAGIC,	"cook",		0,		MFI_NORMAL|MFI_LIGHT,	0,
	cook_setup,	cook_render,	cook_print,	cook_free },

	{MF_MAGIC,	"cmirror",	0,		MFI_NORMAL|MFI_LIGHT,	0,
	cmirror_setup,	cook_render,	cook_print,	cook_free },

	{MF_MAGIC,	"cglass",	0,		MFI_NORMAL|MFI_LIGHT,	0,
	cglass_setup,	cook_render,	cook_print,	cook_free },

	{0,		(char *)0,	0,		0,	0,
	0,		0,		0,		0 }
};

#define RI_AIR		1.0    /* Refractive index of air.		*/

/*
 *			C O O K _ S E T U P
 *
 *  Note:  I can see two ways to set this up.  One is for a (nearly)
 *   colorless object with a given index(s) of refraction.  Compute
 *   the reflect/transmit etc. from that.  The other is for a colored
 *   object where we compute an "effective" set of n's and work from
 *   there.
 */
HIDDEN int
cook_setup( rp, matparm, dpp, mfp, rtip )
register struct region *rp;
struct rt_vls	*matparm;
char	**dpp;
struct mfuncs	*mfp;
struct rt_i	*rtip;
{
	register struct cook_specific *pp;

	RT_VLS_CHECK( matparm );
	GETSTRUCT( pp, cook_specific );
	*dpp = (char *)pp;

	pp->m = 0.2;
	pp->shine = 10;
	pp->wgt_specular = 0.7;
	pp->wgt_diffuse = 0.3;
	pp->transmit = 0.0;
	pp->reflect = 0.0;
	pp->refrac_index = RI_AIR;
	pp->extinction = 0.0;

	/* XXX - scale only if >= 1.0 !? */
	pp->n[0] = (1.0 + sqrt(rp->reg_mater.ma_color[0]*.99))
		   / (1.0 - sqrt(rp->reg_mater.ma_color[0]*.99));
	pp->n[1] = (1.0 + sqrt(rp->reg_mater.ma_color[1]*.99))
		   / (1.0 - sqrt(rp->reg_mater.ma_color[1]*.99));
	pp->n[2] = (1.0 + sqrt(rp->reg_mater.ma_color[2]*.99))
		   / (1.0 - sqrt(rp->reg_mater.ma_color[2]*.99));
	pp->rd[0] = fresnel( 0.0, pp->n[0] ) / rt_pi;
	pp->rd[1] = fresnel( 0.0, pp->n[1] ) / rt_pi;
	pp->rd[2] = fresnel( 0.0, pp->n[2] ) / rt_pi;

	if( bu_struct_parse( matparm, cook_parse, (char *)pp ) < 0 )  {
		rt_free( (char *)pp, "cook_specific" );
		return(-1);
	}

	pp->m2 = ( pp->m < 0.001 ) ? 0.0001 : pp->m * pp->m;
	if( pp->transmit > 0 )
		rp->reg_transmit = 1;
	return(1);
}

/*
 *			M I R R O R _ S E T U P
 */
HIDDEN int
cmirror_setup( rp, matparm, dpp, mfp, rtip )
register struct region *rp;
struct rt_vls	*matparm;
char	**dpp;
struct mfuncs           *mfp;
struct rt_i             *rtip;  /* New since 4.4 release */
{
	register struct cook_specific *pp;

	RT_VLS_CHECK( matparm );
	GETSTRUCT( pp, cook_specific );
	*dpp = (char *)pp;

	pp->m = 0.2;
	pp->shine = 4;
	pp->wgt_specular = 0.6;
	pp->wgt_diffuse = 0.4;
	pp->transmit = 0.0;
	pp->reflect = 0.75;
	pp->refrac_index = 1.65;
	pp->extinction = 0.0;

	pp->n[0] = (1.0 + sqrt(pp->reflect*.99))
		   / (1.0 - sqrt(pp->reflect*.99));
	pp->n[1] = pp->n[2] = pp->n[0];
	pp->rd[0] = fresnel( 0.0, pp->n[0] ) / rt_pi;
	pp->rd[1] = fresnel( 0.0, pp->n[1] ) / rt_pi;
	pp->rd[2] = fresnel( 0.0, pp->n[2] ) / rt_pi;

	if( bu_struct_parse( matparm, cook_parse, (char *)pp ) < 0 )
		return(-1);

	pp->m2 = ( pp->m < 0.001 ) ? 0.0001 : pp->m * pp->m;
	if( pp->transmit > 0 )
		rp->reg_transmit = 1;
	return(1);
}

/*
 *			G L A S S _ S E T U P
 */
HIDDEN int
cglass_setup( rp, matparm, dpp, mfp, rtip )
register struct region *rp;
struct rt_vls	*matparm;
char	**dpp;
struct mfuncs           *mfp;
struct rt_i             *rtip;  /* New since 4.4 release */
{
	register struct cook_specific *pp;

	RT_VLS_CHECK( matparm );
	GETSTRUCT( pp, cook_specific );
	*dpp = (char *)pp;

	pp->m = 0.2;
	pp->shine = 4;
	pp->wgt_specular = 0.7;
	pp->wgt_diffuse = 0.3;
	pp->transmit = 0.8;
	pp->reflect = 0.1;
	/* leaving 0.1 for diffuse/specular */
	pp->refrac_index = 1.65;
	pp->extinction = 0.0;

	pp->n[0] = pp->refrac_index;
	pp->n[1] = pp->n[2] = pp->n[0];
	pp->rd[0] = fresnel( 0.0, pp->n[0] ) / rt_pi;
	pp->rd[1] = fresnel( 0.0, pp->n[1] ) / rt_pi;
	pp->rd[2] = fresnel( 0.0, pp->n[2] ) / rt_pi;

	if( bu_struct_parse( matparm, cook_parse, (char *)pp ) < 0 )
		return(-1);

	pp->m2 = ( pp->m < 0.001 ) ? 0.0001 : pp->m * pp->m;
	if( pp->transmit > 0 )
		rp->reg_transmit = 1;
	return(1);
}

/*
 *			C O O K _ P R I N T
 */
HIDDEN void
cook_print( rp, dp )
register struct region *rp;
char	*dp;
{
	bu_struct_print(rp->reg_name, cook_parse, (char *)dp);
}

/*
 *			C O O K _ F R E E
 */
HIDDEN void
cook_free( cp )
char *cp;
{
	rt_free( cp, "cook_specific" );
}


/*
 *			C O O K _ R E N D E R
 *
 *  El = Il (N.L) dw	Energy from a light (w is solid angle)
 *
 *  I = Sum (r * El)
 *
 *  where, r = kd * rd + ks * rs	(kd + ks = 1, r = Rbd)
 *
 *  rs = F/Pi * [DG/((N.L)(N.S))]
 *  rd = normal reflectance = F(0)/Pi if rough (Lambertian)
 *       This is "a good approx for theta < ~70 degress."
 */
HIDDEN int
cook_render( ap, pp, swp, dp )
register struct application *ap;
struct partition	*pp;
struct shadework	*swp;
char	*dp;
{
	register struct light_specific *lp;
	register fastf_t *intensity, *to_light;
	register int	i;
	register fastf_t cosine;
	register fastf_t refl;
	vect_t	work;
	vect_t	cprod;			/* color product */
	vect_t	h;
	point_t	matcolor;		/* Material color */
	struct cook_specific *ps =
		(struct cook_specific *)dp;
	fastf_t	f, a;
	fastf_t	n_dot_e, n_dot_l, n_dot_h, e_dot_h;
	fastf_t	rd, G, D;
	vect_t	Fv;

	/* XXX - Reflection coefficients - hack until RR_ is changed */
	f = ps->transmit + ps->reflect;
	if( f < 0 ) f = 0;
	if( f > 1.0 ) f = 1.0;
	/*swp->sw_reflect = ps->reflect;*/
	cosine = -VDOT( swp->sw_hit.hit_normal, ap->a_ray.r_dir );
	swp->sw_reflect = fresnel( cosine, ps->refrac_index );
	/*swp->sw_transmit = ps->transmit;*/
	swp->sw_transmit = f - swp->sw_reflect;

	swp->sw_refrac_index = ps->refrac_index;
	swp->sw_extinction = ps->extinction;
	if( swp->sw_xmitonly )
	{
		if( swp->sw_reflect > 0 || swp->sw_transmit > 0 )
			(void)rr_render( ap, pp, swp );
		return(1);	/* done */
	}

	VMOVE( matcolor, swp->sw_color );

	/* ambient component */
	VSCALE( swp->sw_color, matcolor, AmbientIntensity );

	n_dot_e = -VDOT( swp->sw_hit.hit_normal, ap->a_ray.r_dir );
	if( n_dot_e < 0 ) {
		/* Yow, we can't see this point, how did we hit it? */
		rt_log( "cook: N.E < 0\n" );
	}

	/* Consider effects of each light source */
	for( i=ap->a_rt_i->rti_nlights-1; i >= 0; i-- )  {

		if( (lp = (struct light_specific *)swp->sw_visible[i]) == LIGHT_NULL )
			continue;	/* shadowed */
	
		/* Light is not shadowed -- add this contribution */
		intensity = swp->sw_intensity+3*i;
		to_light = swp->sw_tolight+3*i;

		n_dot_l = VDOT( swp->sw_hit.hit_normal, to_light );
		if( n_dot_l < 0 ) {
			/* light through back */
			/*VSET( swp->sw_color, 0, 1, 0 );*/
			continue;
		}

		/* Find H, the bisector of L and E */
		VSUB2( h, to_light, ap->a_ray.r_dir );
		VUNITIZE( h );	/* XXX - warning - L opposite of E */

		n_dot_h = VDOT( swp->sw_hit.hit_normal, h );
		a = acos( n_dot_h );		/*XXXXXX*/
		D = beckmann( a, ps->m2 );	/*XXX Sum k[i]*beck(a,m[i]) */
		e_dot_h = -VDOT( ap->a_ray.r_dir, h );
#if 0
		F = fresnel( e_dot_h, ps->n[0] );
#endif
		Fv[0] = fresnel( e_dot_h, ps->n[0] );
		Fv[1] = fresnel( e_dot_h, ps->n[1] );
		Fv[2] = fresnel( e_dot_h, ps->n[2] );
		G = 1.0;			/*XXXXXX*/

#if 0
		rs = F * G * D / n_dot_e;
#endif
		rd = n_dot_l;			/*XXX ? */

		/* diffuse */
		refl = rd * ps->wgt_diffuse * lp->lt_fraction;
		VELMUL( work, lp->lt_color, intensity );
		VELMUL( cprod, matcolor, work );
		VJOIN1( swp->sw_color, swp->sw_color, refl, cprod );

#ifdef NOCOLORCHANGE
		/* specular */
		refl = rs * ps->wgt_specular * lp->lt_fraction;
/*XXX		VELMUL( work, lp->lt_color, intensity );*/
		VJOIN1( swp->sw_color, swp->sw_color, refl, work );
#else
		refl = G*D/n_dot_e * ps->wgt_specular * lp->lt_fraction;
		VSCALE( Fv, Fv, refl );
		VELMUL( work, work, Fv );
		VADD2( swp->sw_color, swp->sw_color, work );
#endif
	}
	if( swp->sw_reflect > 0 || swp->sw_transmit > 0 )
		(void)rr_render( ap, pp, swp );

	return(1);
}

HIDDEN double
fresnel( c, n )
double	c;	/* cos(theta) = V dot H */
double	n;	/* index of refraction */
{
	double	g, gpc, gmc, t1, t2, f;

	if( n < 1.0 ) {
		fprintf( stderr, "fresnel: can't handle n < 1.0\n" );
		return( 0.0 );
	}
	/* avoid divide by zero.  limit -> 1.0 as theta -> pi/2 */
	if( c < 1.0e-10 )
		return( 1.0 );

	g = sqrt( n*n + c*c - 1.0 );
	gmc = g - c;
	gpc = g + c;
	t1 = c * gpc - 1.0;
	t2 = c * gmc + 1.0;
	f = 0.5 * (gmc*gmc) / (gpc*gpc) * (1.0 + (t1*t1) / (t2*t2));

	return( f );
}
double cos4(a)
double a;
{
	double	c;

	c = cos(a);
	return( c*c*c*c );
}
double tan2(a)
double a;
{
	double	t;

	t = tan(a);
	return( t*t );
}
/*
 *  The Beckmann Distribution
 *
 *              1        - tan^2(a)/m^2
 *   D = -------------- e
 *       m^2 * cos^4(a)
 *
 *  where m = rms slope of microfacets
 *        a = angle between N and H.
 *
 *  Here we are leaving it normalized 0 to 1 by not dividing by m^2.
 */
HIDDEN double
beckmann( a, m2 )
double	a;		/* angle between N and H */
double	m2;		/* rms slope squared (m^2) */
{
	double	t1, t2;

	t1 = cos4(a);		/* note: no m^2 term */
	if( t1 < 1.0e-20 )	/* avoid divide by zero */
		return( 0.0 );

	t2 = exp( -tan2(a)/m2 );

	return( t2/t1 );
}
