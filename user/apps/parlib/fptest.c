#include <arch/softfloat.c>

int main()
{
	softfloat_t sf;
	softfloat_init(&sf);

	volatile double x = 1.0, y = 2.0, z = 3.0;

	volatile long long xx = *(long long*)&x;
	volatile long long yy = *(long long*)&y;
	volatile long long zz = *(long long*)&z;

	for(int i = 0; i < 200000; i++)
		x = y/z;

	printf("%.2f\n",x);

	return 0;
}
