#ifndef EV2_ARRAY_H
#define EV2_ARRAY_H

#include <type_traits>
#include <cstdint>
#include <cstdlib>
#include <cassert>

template<typename T, typename IndexType = uint32_t, int InitialSize = 4, 
	int GrowthNum = 2, int GrowthDenom = 1>
class Array {
	static_assert(std::is_trivially_destructible_v<T>);

	T* _data = nullptr;
	IndexType _capacity = 0;
	IndexType _size = 0;

public:
	~Array()
	{
		if (_data)
			free(_data);
	}

	void ensure_space(size_t bytes)
	{
		if (_data) {
			_data = (T*)realloc(_data, bytes);
		} else {
			_data = (T*)malloc(bytes);
		}
		assert(_data);
	}

	void grow_to(IndexType size)
	{
		if (size > _capacity) {
			IndexType new_capacity = _capacity ? 
				(GrowthNum * _capacity) / GrowthDenom : InitialSize;

			ensure_space(new_capacity * sizeof(T));
			_capacity = new_capacity;
		}
		_size = size;
	}

	inline void push_back(T &&value)
	{
		IndexType old_size = _size;
		grow_to(old_size + 1);
		_data[old_size] = value;
	}

	inline void push_range(const T *start, IndexType count) 
	{
		const IndexType old_size = _size;
		grow_to(old_size + count);
		memcpy(&_data[_size], start, count * sizeof(T)); 
	}

	inline void reserve(IndexType capacity)
	{
		if (capacity <= _capacity)
			return;

		ensure_space(capacity * sizeof(T));
		_capacity = capacity;
	}

	inline void resize(IndexType size)
	{
		ensure_space(size * sizeof(T));
		_size = size;
		_capacity = size;
	}

	IndexType size() const {return _size;} 
	IndexType capacity() const {return _capacity;} 

	T *data() {return _data;}
	const T *data() const {return _data;}

	constexpr const T& operator[] (size_t index) const noexcept
	{
		assert(index < (size_t)_size);
		return _data[index];
	}
	constexpr T& operator[] (size_t index) noexcept
	{
		assert(index < (size_t)_size);
		return _data[index];
	}
};

#endif
