// buggy program - faults with a write to a kernel location


int main(int argc, char** argv)
{ 
	*(unsigned*)0xf0100000 = 0;
	return 0;
}

