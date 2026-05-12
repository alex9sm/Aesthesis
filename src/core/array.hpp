#pragma once

#include "types.hpp"
#include "memory.hpp"


namespace arr {

	template<typename T>
	struct Array {

		T* data = nullptr;
		usize count = 0;
		usize capacity = 0;

	};

	template<typename T>
	Array<T> array_create() {

		return {};

	}

	template<typename T>
	void array_destroy(Array<T>* arr) {

		if (arr->data) {
			memory::free(arr->data);
		}
		*arr = {};

	}

	template<typename T>
	void array_reserve(Array<T>* arr, usize new_cap) {

		if (new_cap <= arr->capacity) return;

		T* new_data = static_cast<T*>(memory::malloc(new_cap * sizeof(T)));

		if (arr->data) {
			memory::copy(new_data, arr->data, arr->count * sizeof(T));
			memory::free(arr->data);
		}

		arr->data = new_data;
		arr->capacity = new_cap;

	}

	template<typename T>
	void array_push(Array<T>* arr, T item) {

		if (arr->count >= arr->capacity) {
			usize new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
			array_reserve(arr, new_cap);
		}

		arr->data[arr->count++] = item;

	}

	template<typename T>
	T array_pop(Array<T>* arr) {

		return arr->data[--arr->count];

	}

	template<typename T>
	void array_resize(Array<T>* arr, usize new_count) {

		if (new_count > arr->capacity) {
			array_reserve(arr, new_count);
		}

		arr->count = new_count;

	}

	template<typename T>
	void array_clear(Array<T>* arr) {

		arr->count = 0;

	}

	template<typename T>
	T& array_at(Array<T>* arr, usize index) {

		return arr->data[index];

	}

	template<typename T>
	T* array_begin(Array<T>* arr) { return arr->data; }

	template<typename T>
	T* array_end(Array<T>* arr) { return arr->data + arr->count; }

}