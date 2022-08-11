# Overview
This program uses GPUDirect RDMA to communicate between server and client GPU.
Source code is created using [the following code](https://github.com/micvbang/rdma).

# Requirements
- [NVIDIA GPU driver](https://www.nvidia.co.jp/Download/index.aspx?lang=us)
- [CUDA](https://docs.nvidia.com/cuda/archive/)
- [Mellanox OFED](https://www.mellanox.com/page/products_dyn?product_family=26)
- [nvidia-peer-memory](https://github.com/Mellanox/nv_peer_memory)

# Operating Environment
The environment used for the operation check is as follows.
There is no guarantee that it will work in other environments.

- OS
  - Server : Linux 4.10.0
  - Client : Linux 4.4.64
- GPU : NVIDIA Quadro M4000
- NIC : Mellanox ConnectX-4 VPI
- Driver
  - NVIDIA GPU driver 375.66
  - CUDA 8.0.61
  - Mellanox OFED 4.1
  - nvidia-peer-memory 1.0.7

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
