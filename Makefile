#==============================Settings================================
CC = gcc
CFLAGS = -Wall -Wextra -g # -O2
LIBS = -libverbs -lrdmacm
CUDA_PATH = /usr/local/cuda-11.7
NVCC = $(CUDA_PATH)/bin/nvcc
CUDA_INCLUDE_DIR = $(CUDA_PATH)/include/
CUDA_LIBRARY_DIR = $(CUDA_PATH)/lib64/ # 32bit=lib, 64bit=lib64
CFLAGS_CUDA = -I$(CUDA_INCLUDE_DIR)
LDFLAGS = -L$(CUDA_LIBRARY_DIR) -lcudart
#======================================================================

all: rdma_server rdma_client

rdma_server.o: rdma_server.c
	$(CC) $(CFLAGS) -c rdma_server.c
rdma_client.o: rdma_client.c
	$(CC) $(CFLAGS) -c rdma_client.c
rdma_common.o: rdma_common.c
	$(CC) $(CFLAGS) -c rdma_common.c
rdma_gpu.o: rdma_gpu.cu
	$(NVCC) $(CFLAGS_CUDA) -c rdma_gpu.cu

rdma_server: rdma_server.o rdma_common.o rdma_gpu.o
	$(NVCC) $(CFLAGS_CUDA) $(LDFLAGS) rdma_server.o rdma_common.o rdma_gpu.o -o rdma_server $(LIBS)
rdma_client: rdma_client.o rdma_common.o rdma_gpu.o
	$(NVCC) $(CFLAGS_CUDA) $(LDFLAGS) rdma_client.o rdma_common.o rdma_gpu.o -o rdma_client $(LIBS)

clean:
	rm -rf *.o rdma_server rdma_client *~
