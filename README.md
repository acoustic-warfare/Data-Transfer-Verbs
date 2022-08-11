# Overview
This program uses GPUDirect RDMA to communicate between server and client GPU.

# Requirements
- [NVIDIA GPU driver](https://www.nvidia.co.jp/Download/index.aspx?lang=us)
- [CUDA](https://docs.nvidia.com/cuda/archive/)
- [Mellanox OFED](https://www.mellanox.com/page/products_dyn?product_family=26)
- [nvidia-peer-memory](https://github.com/Mellanox/nv_peer_memory)

# Operating Environment
The environment used for the operation check is as follows.
There is no guarantee that it will work in other environments.

- OS
  - Server : Linux 5.15.0.41
  - Client : Linux 5.15.0.41
- GPU : NVIDIA Quadro M4000
- NIC : Mellanox ConnectX-6 Dx
- Driver
  - NVIDIA GPU driver 515.65
  - CUDA 11.7
  - Mellanox OFED 
  - nvidia-peer-memory

# Procedure
First, run the following command on the two hosts.
```
$ make
```
After command execution, `rdma_server` and `rdma_client` are created.
Second, run the following command on the server.
```
$ ./rdma_server [-a <server_addr>] [-p <server_port>]
```
Third, run the following command on the client.
```
$ ./rdma_client [-a <server_addr>] [-p <server_port>]
```
