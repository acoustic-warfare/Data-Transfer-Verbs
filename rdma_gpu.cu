#include <cuda.h>
#include <cuda_runtime.h>
#include "rdma_common.h"

struct ibv_mr *mr = NULL;
int totalsize = TOTALSIZE;
int buffsize = BUFFSIZE;
void *addr, *addrserver;

/**
 * @brief All of the functions with an address is run from the transmitter, the rest is ran from the receiver and uses the global *addr.
 * 
 */



/* This function registers RDMA memory region on GPU */
extern "C" struct ibv_mr* rdma_gpubuffer_alloc(struct ibv_pd *pd, uint32_t length,
    enum ibv_access_flags permission)
{
	if (!pd) {
		rdma_error("Protection domain is NULL \n");
		return NULL;
	}

	cudaMalloc((void**)&addr, length);
	if (!addr) {
		rdma_error("failed to allocate buffer, -ENOMEM\n");
		return NULL;
	}
	printf("Allocating gpu memory\n");
	debug("GPU Buffer allocated: %p , len: %u \n", addr, length);

	if (!pd) {
		rdma_error("Protection domain is NULL, ignoring \n");
		return NULL;
	}
	
	debug("GPU pointer address : %p\n", addr);
	mr = ibv_reg_mr(pd, addr, length, permission);
	debug("mr: %p ", mr);
	if (!mr) {
		rdma_error("Failed to create mr on buffer, errno: %s \n", strerror(errno));
		cudaFree(addr);
	}

	debug("Registered: %p , len: %u , stag: 0x%x \n",
	      mr->addr,
	      (unsigned int) mr->length,
	      mr->lkey);

	return mr;
}

extern "C" struct ibv_mr* rdma_gpubuffer_alloc_adress(struct ibv_pd *pd, void* addr, uint32_t length,
    enum ibv_access_flags permission)
{
	if (!pd) {
		rdma_error("Protection domain is NULL \n");
		return NULL;
	}
	//debug("GPU Buffer from server: %p , len: %u \n", addr, length);
	mr = ibv_reg_mr(pd, addr, length, permission);
	debug("mr: %p ", mr);
	if (!mr) {
		rdma_error("Failed to create mr on buffers, errno: %s \n", strerror(errno));
		cudaFree(addr);
	}

	debug("Registered: %p , len: %u , stag: 0x%x \n",
	      mr->addr,
	      (unsigned int) mr->length,
	      mr->lkey);



	return mr;
}

extern "C" int cuAlloc(void** addr, size_t length){
	void *cudmem; 
	cudaError_t err = cudaMallocHost((void**)&cudmem, length);
	if (err != cudaSuccess) {
		rdma_error("Failed to allocate gpumemory, -ENOMEM\n");
		return err;
	}
	debug("GPU Buffer from server: %p , len: %lu \n", cudmem, (unsigned long)length);

	*addr = cudmem;
	return (int)err;

}

extern "C" int cuFree(void* addr){

	cudaError_t err = cudaFree(addr);
	if (err != cudaSuccess) {
		rdma_error("Failed to allocate gpumemory, -ENOMEM\n");
		return -1;
	}
	//printf("Cuda Error? :%d",err);
	return (int)err;
}


/* This function releases RDMA memory region on GPU */
extern "C" void rdma_gpubuffer_free()
{
        if (!mr) {
	       rdma_error("Passed memory region is NULL, ignoring\n");
		return ;
	}
	void *to_free = mr->addr;

	debug("Deregistered: %p , len: %u , stag : 0x%x \n",
	      mr->addr,
	      (unsigned int) mr->length,
	      mr->lkey);
	ibv_dereg_mr(mr);

	debug("Buffer %p free'ed\n", to_free);
	cudaFree(to_free);
}

extern "C" void rdma_gpubuffer_free_addr(struct ibv_mr* mr)
{
        if (!mr) {
	       rdma_error("Passed memory region is NULL, ignoring\n");
		return ;
	}
	void *to_free = mr->addr;

	debug("Deregistered: %p , len: %u , stag : 0x%x \n",
	      mr->addr,
	      (unsigned int) mr->length,
	      mr->lkey);
	ibv_dereg_mr(mr);

	debug("Buffer %p free'ed\n", to_free);
	cudaFree(to_free);
}
/* 
	Function reads the value of the first address. If the memory is equal to idx we know that the transmitter has written the first sequence.
	We then write a -1 in that memory location to signal we've read the written memory.
	If the idx reaches the amount of packages(idx) specified, we break.
*/
__global__ void kernel(void *addr, int totalsize, int buffsize)
{
	volatile int* memory = (int*)addr;
	int idx = 1;
	printf("Clientside GPU \n");
	while(1){		
		if(memory[0] == idx){
			printf("Read full buffer of msg #: %d\n", memory[0]);
				
				//Print out entire written memory buffer
				//for (size_t i = 0; i < buffsize/sizeof(int); i++)
				//{
					//printf(" %d", (memory[i]));
				//}
				
			memory[0] = -1;
			idx++;
		}
		if(idx == 20000)
		{
			memory[0] = -1;
			printf("Finished transfer, sent %d packages.\n", idx);
			break;
		}
	}		
}


__global__ void kernel_addr(void *addr, int totalsize, int buffsize)
{
	int* mem = (int*)addr;
	printf("Serverside GPU Array:");
	for (size_t i = 0; i < buffsize/4; i++)
	{
		printf(" %d", (mem[i]));
	}
	printf("\n");
}

extern "C" void kernel_start()
{
	kernel<<<1, 1>>>(addr, totalsize, buffsize); 
}

extern "C" int cuCopy(void* dst, void* src, size_t size)
{
	cudaError_t err = cudaMemcpy(dst, src, size, cudaMemcpyDeviceToDevice);
	if (err != cudaSuccess) {
		rdma_error("Failed Memcpy: %d\n", err);
	}
	return err;
}

/**
 * @brief Copy a buffer to GPU memory.
 * 
 */
extern "C" void kernel_start_addr(void *addr)
{
	int test[buffsize] = {0};
	for (int i = 0; i < buffsize; i++)
	{
		test[i] = i;
	}
	debug("buffsize: %d\n",buffsize);
	cudaError_t err = cudaMemcpy(addr, (void*)&test, buffsize, cudaMemcpyDefault);
	if (err != cudaSuccess) {
		rdma_error("Failed Memcpy\n");
	}

}