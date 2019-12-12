#include <cuda.h>
#include <stdio.h>
#include <string.h>

__global__ void CountSort(int*, int*, int, int);

__host__ void counting_sort(int* arr, int size, int max_val)
{
	int block_num = 5;
	int thread_num_per_block = 5;
	uint64_t histo_size = sizeof(int)*max_val;
	printf("size: %d\n", size);
	printf("max_val: %d\n", max_val);
	printf("block_num: %d\n", block_num);
	printf("thread_per_block: %d\n", thread_num_per_block);

	int* darr;
	cudaMalloc(&darr, sizeof(int)*size);
	cudaMemcpy(darr, arr, sizeof(int)*size, cudaMemcpyHostToDevice); 

	int* dhisto;
	cudaMalloc(&dhisto, histo_size);
	cudaMemset(dhisto, 0, histo_size);

	printf("countsort start\n");
	CountSort<<<block_num, thread_num_per_block>>>(darr, dhisto, size, max_val);
	printf("countsort end\n");
	
	int* histo = (int*)calloc(max_val, sizeof(int)); 
	cudaMemcpy(histo, dhisto, sizeof(int)*max_val, cudaMemcpyDeviceToHost);
	cudaMemcpy(arr, darr, sizeof(int)*size, cudaMemcpyDeviceToHost);
	
	
	//printf("total_cnt: %d\n", histo[max_val-1]);
	
	/*
	int idx = 0;
	for(int i=0; i<max_val; i++) {
		for(int j=0; j<histo[i]; j++) {
			arr[idx++] = i;
		}
	}
	*/
	//cudaFree(dhisto);
	//cudaFree(darr);
	//free(histo);
}

__global__ void CountSort(int* darr, int* dhisto, int size, int max_val) {

	uint64_t thread_per_block = blockDim.x;
	uint64_t total_block = gridDim.x;
	uint64_t bid = blockIdx.x;
	uint64_t tid = threadIdx.x;
	uint64_t size_per_block, bstart, size_per_thread, start, end;

	// update histogram	in each block
	if(size % total_block != 0 && bid == total_block - 1) {
		size_per_block = size / total_block + size % total_block;
		bstart = bid * (size / total_block);
		size_per_thread = size_per_block / thread_per_block;
		start = bstart + tid * size_per_thread;
		end = start + size_per_thread;
		if(size_per_block % thread_per_block != 0 && 
				tid == thread_per_block - 1) {
			end += size_per_block % thread_per_block;
		}
	}
	else {
		size_per_block = size / total_block;
		bstart = bid * size_per_block;	
		size_per_thread = size_per_block / thread_per_block;
		start = bstart + tid * size_per_thread;
		end = start + size_per_thread;
		if(size_per_block % thread_per_block != 0 && tid == thread_per_block - 1) {
			end += size_per_block % thread_per_block;
		}
	}
	for(uint64_t i=start; i<end; i++) {
		atomicAdd(&dhisto[darr[i]], 1);
	}
	__syncthreads();
	

	if(bid == 0 && tid == 0 ) {
		for(int i=1; i<max_val; i++) {
			dhisto[i] += dhisto[i-1];
		}
		printf("%d\n", dhisto[max_val-1]);
	}

	__syncthreads();

	size_per_block = max_val / total_block;
	bstart = bid * size_per_block;
	size_per_thread = size_per_block / thread_per_block;
	start = bstart + tid * size_per_thread;
	end = start + size_per_thread;

	for(int i=start; i<end; i++) {
		if(i == 0) {
			for(int j=0; j<dhisto[0]; j++)
				darr[j] = i;
		}
		else {
			for(int j=dhisto[i-1]; j<dhisto[i]; j++) {
				darr[j] = i;	
			}
		}
	}
	
	__syncthreads();
}
