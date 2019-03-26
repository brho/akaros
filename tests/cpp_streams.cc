#include <iostream>
#include <fstream>
#include <string>
#include <errno.h>

#include <memory>

using namespace std;

struct foobar {
	int x;
};

int main(void)
{
	string line;
	ifstream myfile;
	/* grep the asm for M_release to verify we're using atomics */
	std::shared_ptr<foobar> foo = make_shared<foobar>();

	errno = 0;
	myfile.open("hello.txt", ifstream::in);
	if (errno)
		perror("Unable to open (hello.txt):");
	if (myfile.is_open()) {
		while (myfile.good()) {
		  getline(myfile, line);
		  cout << line << endl;
		}
		myfile.close();
		cout << "Stream test passed" << endl;
	} else {
		cout << "Unable to open file"; 
	}
	return 0;
}
