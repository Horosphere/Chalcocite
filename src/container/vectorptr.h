#ifndef CHALCOCITE_CONTAINER_VECTORPTR_H_
#define CHALCOCITE_CONTAINER_VECTORPTR_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * @warning This has not been optimised for performance.
 * @brief An array of pointers
 */
typedef struct
{
	void** data;
	size_t size;
} VectorPtr;

void VectorPtr_init(VectorPtr* const);
void VectorPtr_destroy(VectorPtr* const);

void VectorPtr_clear(VectorPtr* const);
size_t VectorPtr_size(VectorPtr* const);
void* VectorPtr_at(VectorPtr* const, size_t index);
bool VectorPtr_push_back(VectorPtr* const, void*);
bool VectorPtr_remove(VectorPtr* const, size_t index);

#endif // !CHALCOCITE_CONTAINER_VECTORPTR_H_
