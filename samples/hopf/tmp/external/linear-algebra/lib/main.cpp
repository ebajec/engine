#include <iostream>
#include <vector>
#include <complex>
#include "matrix.hpp"

#define PI 3.14159265359

using namespace std;

using vec3 = matrix<3, 1>;
using mat3 = matrix<3, 3>;

vec3 n = vec3::random(10);
vec3 l = vec3::random(10);
vec3 l_o = vec3::random(10);


bool split_test() {
	constexpr int rows = 17;
	constexpr int cols = 13;

	auto test = [](matrix<rows,cols,int> A) {
		auto pair = A.split<rows/2>();

		auto A_new = pair.first|pair.second;

		return A - A_new == matrix<rows,cols,int>(0);
	};
	 
	auto A = matrix<rows,cols,int>::random(1000,1000);

	return test(A);
}

template<typename T>
bool zero_test(T* data, size_t size) {
	for (int i = 0; i < size; i++) {
		if (data[i] > 1.192092896e-07F/*FLT_EPSILON*/) return false;
	}
	return true;
}

/*
* Attemps to solve matrix equation AX = B, where A is (m*n), X and B are (m*l).
* prints results and returns solution. 
*/
template <int m, int n, int l, typename F>
matrix<m,l,F> solve(matrix<m,n,F> A, matrix<m,l,F> B) {
	std::cout << "\nRREF test running:\n";
	matrix<m,n+l,F> augmented = A|B;
	auto RREF = rref(augmented);

	auto split_pair = RREF.template split<n>();
	auto A_new = split_pair.first;
	auto B_new = split_pair.second;

	std::cout << "Reduced system is:\n";
	RREF.print();

	std::cout << "difference is\n";
	auto diff = B - A*B_new;
	diff.print();

	if (zero_test(diff.data(),l*m)) {
		std::cout << "solution is:\n";
		B_new.print();
	}
	else {
		std::cout << "system is inconsistent\n";
	}

	return B_new;
}

template <int m, int n,typename F>
vector<matrix<m,n,F>> ker(matrix<m,n,F> A) {
	list<int> ones;
	auto reduced = RREF(A,&ones);

	vector<int> one_list;

	for (int j = 0; j < n; j++) {
		int one = ones.front;
		if (j == one) {
			one_list.push_back(one);
			ones.pop_front();
		}
		else {
			auto null_col = reduced.col(j);
			matrix<n,1> ker_elem((F)0);
		}
	}
}

template<typename func>
void test(func F, string name) {
	if (F()) {
		cout << "Test passed ✅ : " << name << "\n";
	}
	else {
		cout << "Test failed ❌: " << name << "\n";
	}
}

using namespace std;
int main()
{
	test(split_test,"matrix.split()");

	matrix<3,3> A = {
		0,1,2,
		0,0,0,
		2,0,3
	};

	matrix<3,2> B = {
		1,0,
		0,0,
		1,1};

	solve(A,B);

	return 0;
}