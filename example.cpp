#include "Pattern.h"

using namespace std;

int main()
{
	off_t ch1[100] = {0,3,7,14,17,21,28,31,35,42,46,50,54,58};
	off_t ch2[100] = {0,2,4,8,10,12,16,18,20,24,27,30,33,36};
	size_t ch3[100] = {2,2,4,2,2,4,2,2,4,3,3,3,3,3};
	pid_t id = 4287;
	for(int i = 0; i < 13; ++i)
	{
		add_off(ch1[i], ch2[i], ch3[i]);
	}
	pattern_detection(id);
	print_global();
	return 0;
}
