#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <atomic>

#include <cstddef>
#include <cmath>

#ifndef TWOPI
#define TWOPI 6.28318530718
#endif

#ifndef PI
#define PI 3.14159265359
#endif

static inline double urandf1()
{
	return (1.0 - (double)rand())/(double)RAND_MAX;
}

/// @brief smooth function that is zero up to the first derivative at -1 and 1
static inline double filter_band(double x)
{
	double a = 1 - x*x*x*x;
	return 2*a*a/(1 + a*a);
}

static inline double smooth_max_zero(double x)
{
	static constexpr double b = 6;

	return -(1.0/(b*b))*log(1/(1.0 + exp((b*b)*x)));
}

static double weierstrass(double x, double y, float phase = 0)
{
	static constexpr double 
	L = 1.2, 
	D = 2.3,
	G = 4, 
	gamma =	2.4;
	static constexpr size_t M = 9, N = 9;

	double A = L*pow(G/D,D-2.0)*sqrt(log(gamma)/(double)M); 

	static std::atomic_int init = 0;

	static double phi[M][N];
	static double cos_phi[M][N];

	static double gammaD3n[N];
	static double gamman[N];

	if (!init++) {
		for (size_t m = 0; m < M; ++m) {
			for (size_t n = 0; n < N; ++n) {
				phi[m][n] = TWOPI*urandf1();
				cos_phi[m][n] = cos(phi[m][n]);
			}
		}

		for (size_t n = 0; n < N; ++n) {
			gammaD3n[n] = pow(gamma, (D - 3.0)*n);
			gamman[n] = pow(gamma, n);
		}
	}

	double g = 0;

	double r = hypot(x,y);
	double tht = atan2(y,x);

	for (size_t m = 0; m < M; ++m) {
		for (size_t n = 0; n < N; ++n) {
			double phi_mn = phase + phi[m][n];

			g += gammaD3n[n] * (cos_phi[m][n] - cos(TWOPI*gamman[n]*r*cos(tht - PI*m/M)/L + phi_mn));
		}
	}
	return A*g;

}

#endif
