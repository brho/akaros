#include <iostream>
#include <fstream>
#include <string>
#include <errno.h>

using namespace std;

int main() {
	string line;
	ifstream myfile;
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
