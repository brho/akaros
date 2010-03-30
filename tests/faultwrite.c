// buggy program - faults with a write to location zero

int main(int argc, char** argv)
{
	*(unsigned*)0 = 0;
	return 0;
}

