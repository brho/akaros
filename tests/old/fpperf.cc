/* Chunk of code from stage 6 of fluidanimate, running on its own in a single
 * process, meant to run on either linux or akaros (check the ifdef below). */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <sys/time.h>
#include <pthread.h>

typedef float fptype;

class Vec3
{
public:
  fptype x, y, z;

  Vec3() {}
  Vec3(fptype _x, fptype _y, fptype _z) : x(_x), y(_y), z(_z) {}

  fptype  GetLengthSq() const         { return x*x + y*y + z*z; }
  fptype  GetLength() const           { return sqrtf(GetLengthSq()); }
  Vec3 &  Normalize()                 { return *this /= GetLength(); }

  bool    operator == (Vec3 const &v) { return (x == v.x) && (y == v.y) && (z += v.z); }
  Vec3 &  operator += (Vec3 const &v) { x += v.x;  y += v.y; z += v.z; return *this; }
  Vec3 &  operator -= (Vec3 const &v) { x -= v.x;  y -= v.y; z -= v.z; return *this; }
  Vec3 &  operator *= (fptype s)      { x *= s;  y *= s; z *= s; return *this; }
  Vec3 &  operator /= (fptype s)      { fptype tmp = 1.f/s; x *= tmp;  y *= tmp; z *= tmp; return *this; }

  Vec3    operator + (Vec3 const &v) const    { return Vec3(x+v.x, y+v.y, z+v.z); }
  Vec3    operator + (fptype const &f) const  { return Vec3(x+f, y+f, z+f); }
  Vec3    operator - () const                 { return Vec3(-x, -y, -z); }
  Vec3    operator - (Vec3 const &v) const    { return Vec3(x-v.x, y-v.y, z-v.z); }
  Vec3    operator * (fptype s) const         { return Vec3(x*s, y*s, z*s); }
  Vec3    operator / (fptype s) const         { fptype tmp = 1.f/s; return Vec3(x*tmp, y*tmp, z*tmp); }

  fptype  operator * (Vec3 const &v) const    { return x*v.x + y*v.y + z*v.z; }
};

float rand_f1 = 3.14;
float rand_f2 = 6.282222;
float rand_f3 = 443.38383;
float rand_f4;
float rand_f5;
static const fptype doubleRestDensity = 2000.0;
static const fptype viscosity = 0.4;

static const Vec3 rand_v3_1(0.1, -9.8, 30.0);
static const Vec3 rand_v3_2(0.2, -90.8, 0.5);
static const Vec3 rand_v3_3(7.3, -444.8, 8.0);
static const Vec3 rand_v3_4(8.4, -99.8, 0.8);

Vec3 output(0, 0, 0);

int main(int argc, char **argv)
{
	struct timeval start_tv = {0};
	struct timeval end_tv = {0};
	uint64_t usec_diff;
	if (argc < 3) {
		printf("Need 2 args\n");
		exit(-1);
	}

	/* Disable this if you want to compile for linux with the i686-ros-g++ */
	#if 1
	# ifdef __ros__
	if (argc == 4) {
		pthread_can_vcore_request(FALSE);
		pthread_lib_init();				
		printf("Vcore %d mapped to pcore %d\n", 0, __procinfo.vcoremap[0].pcoreid);
	}
	# endif
	#endif

	rand_f4 = (float)atoi(argv[1]);
	rand_f5 = (float)atoi(argv[2]);
	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");
	for (int i = 0; i < 300000000; i++) {
	
		Vec3 disp = rand_v3_1 - rand_v3_2 * rand_f5;
		fptype distSq = disp.GetLengthSq();
		fptype dist = sqrtf(std::max(distSq, (fptype)1e-12));
		fptype hmr = rand_f1 - dist;
		
		Vec3 acc = disp * rand_f2 * (hmr*hmr/dist)
		           * (rand_f3 + rand_f4 + rand_f5 - doubleRestDensity);
		acc += (rand_v3_3 - rand_v3_4 * rand_f5) * viscosity * hmr;
		acc /= rand_f5 * rand_f1;
		
		output += acc;
	}
	float ret = output.GetLengthSq();
	if (gettimeofday(&end_tv, 0))
		perror("End time error...");
	usec_diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 +
	            (end_tv.tv_usec - start_tv.tv_usec);
	printf("%f, took %.3f sec\n", ret, (float)usec_diff / 1000000);
	return 0;
}
