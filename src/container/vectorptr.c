#include "vectorptr.h"

#include <assert.h>
#include <memory.h>
#include <stdlib.h>

void VectorPtr_init(VectorPtr* const vp)
{
	memset(vp, 0, sizeof(VectorPtr));
}
void VectorPtr_destroy(VectorPtr* const vp)
{
	free(vp->data);
}

void VectorPtr_clear(VectorPtr* const vp)
{
	free(vp->data);
	vp->data = NULL;
	vp->size = 0;
}
size_t VectorPtr_size(VectorPtr* const vp)
{
	return vp->size;
}
void* VectorPtr_at(VectorPtr* const vp, size_t index)
{
	assert(index < vp->size);
	return vp->data[index];
}
bool VectorPtr_push_back(VectorPtr* const vp, void* ptr)
{
	void** temp = realloc(vp->data, vp->size + 1);
	if (!temp) return false;
	vp->data = temp;

	vp->data[vp->size] = ptr;
	++vp->size;
	return true;
}
bool VectorPtr_remove(VectorPtr* const vp, size_t index)
{
	assert(index < vp->size);
	if (index + 1 == vp->size) // Removing last element
	{
		if (vp->size > 1)
		{
			void** temp = realloc(vp->data, vp->size - 1);
			if (!temp) return false;
			--vp->size;
		}
		else
		{
			vp->size = 0;
			vp->data = NULL;
		}
	}
	else
	{
		void* last = vp->data[vp->size - 1];
		void** temp = realloc(vp->data, vp->size - 1);
		if (!temp) return false;
		--vp->size;
		for (size_t i = index; i < vp->size - 1; ++i)
		{
			vp->data[i] = vp->data[i + 1];
		}
		vp->data[vp->size - 1] = last;
	}
	return true;
}
