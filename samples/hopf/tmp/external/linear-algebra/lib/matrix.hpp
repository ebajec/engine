// this just contains implementation garbage
#ifndef MATRIX_HPP
#define MATRIX_HPP

#include <initializer_list>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include "math.h"
#include "matrix.h"

#define DBL_EPSILON 2.2204460492503131e-016

using namespace std;

// constructors
template <int m, int n, typename F>
matrix<m, n, F>::matrix()
{
}

template <int m, int n, typename F>
matrix<m, n, F>::matrix(F val) {
	for (int i = 0; i < m * n; i++)
	{
		mem[i] = val;
	}
}

template <int m, int n, typename F>
matrix<m, n, F>::matrix(const matrix<m, n, F> &other)
{
	for (int i = 0; i < m * n; i++)
	{
		mem[i] = other[0][i];
	}
}

template <int m, int n, typename F>
matrix<m, n, F>::matrix(const initializer_list<F> &arr)
{
	int i = 0;
	for (F x : arr)
	{
		mem[i] = x;
		i++;
	}
}

template <int m, int n, typename F>
matrix<m, n, F>::matrix(F *new_data)
{
	*this = matrix();

	for (int i = 0; i < m * n; i++)
	{
		this->mem[i] = new_data[i];
	}
}

// operator overloads

template <int m, int n, typename F>
matrix<m, n, F> matrix<m, n, F>::operator+(const matrix<m, n, F> &B) const
{
	matrix<m, n, F> new_mat;
	for (int i = 0; i < m * n; i++)
	{
		new_mat.mem[i] += this->mem[i] + B.mem[i];
	}

	return new_mat;
}

template <int m, int n, typename F>
matrix<m, n, F> matrix<m, n, F>::operator-(const matrix<m, n, F> &B) const
{
	matrix<m, n, F> new_mat;
	for (int i = 0; i < m * n; i++)
	{
		new_mat.mem[i] += this->mem[i] - B.mem[i];
	}

	return new_mat;
}

template <int m, int n, typename F>
matrix<m, n, F> matrix<m, n, F>::operator*(F const &c) const
{
	matrix<m, n, F> new_mat;
	for (int i = 0; i < m * n; i++)
	{
		new_mat.mem[i] = mem[i] * c;
	}

	return new_mat;
}

template <int m, int n, typename F>
template <int l>
matrix<m, l, F> matrix<m, n, F>::operator*(const matrix<n, l, F> &B) const
{
	// need to do this because matrix<n, l, F> is a different type and access
	// to private fields is not allowed.
	F new_data[m * l] = {0};

	for (int i = 0; i < m; i++)
	{
		for (int j = 0; j < l; j++)
		{
			for (int k = 0; k < n; k++)
			{
				new_data[i * l + j] += mem[i * n + k] * B[k][j];
			}
		}
	}
	return matrix<m, l, F>(new_data);
}

template <int m, int n, typename F>
template <int l>
inline matrix<m, n + l, F> matrix<m, n, F>::operator|(const matrix<m, l, F> &other) const
{
	F new_data[m * (l + n)] = {0};

	const int cols = n + l;
	for (int i = 0; i < m; i++)
	{
		for (int j = 0; j < n; j++)
		{
			new_data[i * cols + j] = mem[i * n + j];
		}
		for (int j = 0; j < l; j++)
		{
			new_data[i * cols + j + n] = other[0][i * l + j];
		}
	}

	return matrix<m, n + l, F>(new_data);
}

template <int m, int n, typename F>
matrix<m, n, F> matrix<m, n, F>::operator^(const int &pow) const
{
	static_assert(m == n, "matrix must be square");
	if (pow < 0)
	{
		throw std::invalid_argument("Negative matrix powers are not always defined");
	}

	matrix<m, n, F> new_mat = matrix<m, n, F>::id();

	for (int i = 0; i < pow; i++)
	{
		new_mat = new_mat * (*this);
	}

	return new_mat;
}

template <int m, int n, typename F>
bool matrix<m, n, F>::operator==(const matrix<m, n, F> &other) const
{
	for (int i = 0; i < m * n; i++)
	{
		//I know this is not perfect, but what is
		if (abs(mem[i] - other[0][i]) >  DBL_EPSILON) return false;
	}
	return true;
}

template <int m, int n, typename F>
bool matrix<m, n, F>::operator!=(const matrix<m, n, F> &other) const
{
	return !(*this == other);
}


template <int m, int n, typename F>
void matrix<m, n, F>::print() const
{
	for (int i = 0; i < m * n; i++)
	{
		std::cout << mem[i] << ' ';

		if ((i + 1) % n == 0)
		{
			std::cout << '\n';
		}
	}
	std::cout << '\n';
}

template <int m, int n, typename F>
matrix<1, n, F> matrix<m, n, F>::row(int i) const
{
	matrix<1, n, F> new_mat;

	for (int j = 0; j < n; j++)
	{
		new_mat[0][j] = mem[i * n + j];
	}

	return new_mat;
}

template <int m, int n, typename F>
matrix<m, 1, F> matrix<m, n, F>::col(int j) const
{
	matrix<m, 1, F> new_mat;
	for (int i = 0; i < m; i++)
	{
		new_mat[0][i] = mem[i * n + j];
	}

	return new_mat;
}

template <int m, int n, typename F>
matrix<m - 1, n - 1, F> matrix<m, n, F>::submatrix(int row, int col) const
{
	assert((row < m) && (col < n));

	matrix<m - 1, n - 1, F> new_mat;

	if (m == 1 || n == 1)
	{
		return new_mat;
	}

	F *new_data = new_mat.data();

	int k = 0;

	for (int i = 0; i < m; i++)
	{
		for (int j = 0; j < n; j++)
		{
			if (!(i == row || j == col))
			{
				new_data[k] = mem[i * n + j];
				k++;
			}
		}
	}

	return new_mat;
}

template <int m, int n, typename F>
template<int k>
pair<matrix<m,k,F>,matrix<m,n-k,F>> matrix<m, n, F>::split() {
	static_assert(0 < k < n);
	matrix<m,k,F> left((F)0);
	matrix<m,n-k,F> right((F)0);

	for (int row = 0; row < m; row++) {
		for (int col = 0; col < k; col++) {
			left[row][col] = mem[row*n + col];
		}	
		for (int col = k; col < n; col++) {
			right[row][col - k] = mem[row*n + col];
		}		
	}

	return {left,right};
}

template <int m, int n, typename F>
matrix<n, m, F> matrix<m, n, F>::transpose() const
{
	matrix<n, m, F> new_mat;

	for (int i = 0; i < m; i++)
	{
		for (int j = 0; j < n; j++)
		{
			new_mat[0][j * m + i] = mem[i * n + j];
		}
	}

	return new_mat;
}


template <int m, int n, typename F>
void matrix<m, n, F>::mult_row(int dest, F c) {
	for (int col = 0; col < n; col++) {
		mem[dest*n + col] *= c; 
	}
	return;
}

template <int m, int n, typename F>
void matrix<m, n, F>::add_to_row(int dest, matrix<1, n, F> r) {
	for (int col = 0; col < n; col++) {
		mem[dest*n + col] += r[0][col]; 
	}
	return;
}

template <int m, int n, typename F>
template <int k, int l>
matrix<m + k, n + l, F> matrix<m, n, F>::direct_sum(const matrix<k, l, F> &other) const
{
	F new_data[(m + k) * (n + l)] = {0};

	// This looks gross, but I would have to use the modulo operator a lot
	// to do it differently. I feel like this would be faster.
	for (int i = 0; i < m; i++)
	{
		for (int j = 0; j < n; j++)
		{
			new_data[i * (n + l) + j] = mem[i * n + j];
		}
	}
	for (int i = 0; i < k; i++)
	{
		for (int j = 0; j < l; j++)
		{
			new_data[(i + m) * (n + l) + j + n] = other[0][i * l + j];
		}
	}

	return matrix<m + k, n + l, F>(new_data);
}

template <int m, int n, typename F>
template <int k, int l>
matrix<m * k, n * l, F> matrix<m, n, F>::kronecker_prod(const matrix<k, l, F> &B) const
{
	F new_data[m * k * n * l] = {0};

	// pretend matrix 'A' is this instance
	for (int a_row = 0; a_row < m; a_row++)
	{
		for (int a_col = 0; a_col < n; a_col++)
		{
			for (int b_row = 0; b_row < k; b_row++)
			{
				for (int b_col = 0; b_col < l; b_col++)
				{

					int index = a_row * k * n * l + b_row * n * l + a_col * l + b_col;
					new_data[index] = this->mem[a_row * n + a_col] * B[b_row][b_col];
				}
			}
		}
	}

	return matrix<m * k, n * l>(new_data);
}

template <int m, int n, typename F>
matrix<m, n, F> matrix<m, n, F>::id()
{
	static_assert(m == n, "matrix must be square");

	matrix<m, n, F> identity;
	for (int i = 0; i < n; i++)
	{
		identity[0][i * n + i] = 1;
	}
	return identity;
}

template <int m, int n, typename F>
matrix<m, n, F> matrix<m, n, F>::random(F max_val, int fineness)
{
	F data[m * n];

	for (int i = 0; i < m * n; i++)
	{
		data[i] = max_val * ((rand() % fineness) / (double)fineness);
	}

	return matrix<m, n, F>(data);
}

/******NON MEMBER FUNCTIONS*****/

template <int m, int n, typename F>
matrix<m, n, F> gauss_elim(matrix<m, n, F> A)
{
	F data[m * n];
	for (int i = 0; i < m * n; i++)
	{
		data[i] = A[0][i];
	}

	// add c*r1 to r2
	auto row_add = [&data](int r1, int r2, F c)
	{
		for (int j = 0; j < n; j++)
		{
			data[r2 * n + j] += c * data[r1 * n + j];
		}
		return;
	};
	// swap r1 and r2
	auto row_swap = [&data](int r1, int r2)
	{
		for (int i = 0; i < n; i++)
		{
			F temp = data[r1 * n + i];
			data[r1 * n + i] = data[r2 * n + i];
			data[r2 * n + i] = temp;
		}
		return;	
	};

	int row_nonzero = 0;
	for (int col = 0; col < n; col++)
	{
		F *top_entry = data + row_nonzero * n + col;

		// this attempts to swap rows and make top_entry nonzero
		if (abs(*top_entry) < DBL_EPSILON)
		{
			for (int row = row_nonzero; row < m; row++)
			{
				if (abs(data[row * n + col]) > DBL_EPSILON)
				{
					row_swap(row_nonzero, row);
					break;
				}
			}
		}

		// we only continue if last part was successful
		F top_entry_val = *top_entry;
		if (abs(top_entry_val) > DBL_EPSILON)
		{
			// now we add row_nonzero to the ones below to get zeroes
			for (int row = row_nonzero + 1; row < m; row++)
			{
				F val = data[row * n + col];
				if (abs(val) > DBL_EPSILON)
				{
					row_add(row_nonzero, row, -val * (1 / top_entry_val));
				}
			}
			// go to next row once we're done
			row_nonzero++;
		}
	}

	return matrix<m, n, F>(&data[0]);
}

/* Computes reduced row echelon form of matrix.
@param ones [optional] use to store information for leading one columns
*/
template <int m, int n, typename F>
matrix<m, n, F> rref(matrix<m, n, F> A, list<int>* ones) {
	matrix<m,n,F> gaussian = gauss_elim(A);

	list<int>* leading_ones = ones;
	if (ones == nullptr) {
		leading_ones = new list<int>;
	}

	for (int row = 0; row < m; row++) {
		for (int col = row; col < n; col++) {

			F leading = gaussian[row][col];
			if (abs(leading) > 1.192092896e-07F) {
				gaussian.mult_row(row,1/leading);
				leading_ones->push_back(col);
				break;
			} 
		}
	}
	int one_row = 0;
	for (int one_col : *leading_ones) {
		for (int row = one_row - 1; row >= 0; row--) {
			gaussian.add_to_row(row,-((F)1)*gaussian[row][one_col]*gaussian.row(one_row));
		}
		one_row++;
	}

	return gaussian;

}

/*Compute determinant via laplacian expansion*/
template <int n, typename F>
F det_laplace(matrix<n, n, F> A)
{

	auto alt_sign = [](int k)
	{
		int sign = k % 2;
		return 1 - 2 * sign;
	};

	if (n == 0)
	{
		return 1;
	}
	if (n == 1)
	{
		return A[0][0];
	}
	else
	{
		F value = 0;

		for (int i = 0; i < n; i++)
		{
			value += alt_sign(i) * (A[0][i] * det_laplace(A.submatrix(0, i)));
		}
		return value;
	}
}

template <int n, typename F>
matrix<n, n, F> inv(matrix<n, n, F> A)
{
	F det = det_laplace(A);

	if (abs(det) < DBL_EPSILON)
	{
		throw std::invalid_argument("Matrix must be invertible");
	}

	return pow(det, -1) * adj(A);
}

/*
 * Computes adjucate matrix.
 */
template <int n, typename F>
matrix<n, n, F> adj(const matrix<n, n, F> &A)
{
	auto alt_sign = [](int k)
	{
		int sign = k % 2;
		return 1 - 2 * sign;
	};

	if (n == 1)
	{
		return matrix<n, n, F>::id();
	}

	matrix<n, n, F> adjucate;
	for (int i = 0; i < n; i++)
	{
		for (int j = 0; j < n; j++)
		{
			adjucate[j][i] = alt_sign(i + j) * det_laplace(A.submatrix(i, j));
		}
	}
	return adjucate;
}

template <int n, typename F>
F dot(const matrix<n, 1, F> &A, const matrix<n, 1, F> &B)
{
	F val = 0;
	for (int i = 0; i < n; i++)
	{
		F a = A[0][i];
		F b = B[0][i];
		val += a * b;
	}

	return val;
}

template <typename F>
matrix<3, 3, F> cross_mat(const matrix<3, 1, F> &v)
{
	return matrix<3, 3, F>({0, v[2][0] * -1, v[1][0],
							v[2][0], 0, v[0][0] * -1,
							v[1][0] * -1, v[0][0], 0});
}

template <typename F>
matrix<3, 1, F> cross(const matrix<3, 1, F> &v, const matrix<3, 1, F> &w)
{
	return cross_mat(v) * w;
}

template <int n, typename F>
matrix<n, 1, F> normalize(const matrix<n, 1, F> &v)
{
	return v * (1 / sqrt(dot(v, v)));
}


template <typename F,typename T>
matrix<3, 3, F> rot_axis(matrix<3, 1, F> k, T theta)
{
	k = normalize(k);
	return matrix<3, 3, F>::id() * cos(theta) + cross_mat(k) * sin(theta) + (k * (k.transpose())) * (1 - cos(theta));
}

template <typename F,typename T>
matrix<3, 3, F> rotatexy(T angle)
{
	return matrix<3, 3, F>(
		{cos(angle), -sin(angle), 0,
		 sin(angle), cos(angle), 0,
		 0, 0, 1});
}

template <typename F,typename T>
matrix<3, 3, F> rotatexz(T angle)
{
	return matrix<3, 3, F>(
		{cos(angle), 0, -sin(angle),
		 0, 1, 0,
		 sin(angle), 0, cos(angle)});
}

template <typename F,typename T>
matrix<3, 3, F> rotateyz(T angle)
{
	return matrix<3, 3, F>(
		{1, 0, 0,
		 0, cos(angle), -sin(angle),
		 0, sin(angle), cos(angle)});
}

#endif
