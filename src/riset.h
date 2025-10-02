#ifndef _RISET_H_
#define _RISET_H_
/* riset.h
 * @(#)riset.h	1.2 21 Nov 1995
 *
 * Calculation of rise/set times and azimuths 
 *
 * Steve Groom, JPL
 * November, 1995
 */

#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN extern
#endif

EXTERN void riset(double ra, double dec, double lat, double dis,
		double *lstr, double *lsts, double *azr, double *azs,
		int *status);

#endif /* _RISET_H_ */
