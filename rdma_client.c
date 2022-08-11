#include <unistd.h>
#include "rdma_common.h"
/* These are basic RDMA resources */
/* These are RDMA connection related resources */
static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *client_cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp;
/* These are memory buffers related resources */
static struct ibv_mr *client_metadata_mr = NULL, *client_buffer_mr = NULL, *server_metadata_mr = NULL;
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
static struct ibv_sge client_send_sge, server_recv_sge;
				 //2^19
//#define TOTALSIZE (TOTALSIZE) /* Total memory region size (BUFFSIZE*2) */

/* This function prepares client side connection resources for an RDMA connection */
static int client_prepare_connection(struct sockaddr_in *s_addr)
{
	struct rdma_cm_event *cm_event = NULL;
	int ret = -1;
	/*  Open a channel used to report asynchronous communication event */
	cm_event_channel = rdma_create_event_channel();
	if (!cm_event_channel) {
		rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
		return -errno;
	}
	debug("RDMA CM event channel is created at : %p \n", cm_event_channel);
	/* rdma_cm_id is the connection identifier (like socket) which is used
	 * to define an RDMA connection. */
	ret = rdma_create_id(cm_event_channel, &cm_client_id,
			NULL,
			RDMA_PS_TCP);
	if (ret) {
		rdma_error("Creating cm id failed with errno: %d \n", -errno);
		return -errno;
	}
	/* Resolve destination and optional source addresses from IP addresses  to
	 * an RDMA address.  If successful, the specified rdma_cm_id will be bound
	 * to a local device. */
	ret = rdma_resolve_addr(cm_client_id, NULL, (struct sockaddr*) s_addr, 2000);
	if (ret) {
		rdma_error("Failed to resolve address, errno: %d \n", -errno);
		return -errno;
	}
	debug("waiting for cm event: RDMA_CM_EVENT_ADDR_RESOLVED\n");
	ret  = process_rdma_cm_event(cm_event_channel,
			RDMA_CM_EVENT_ADDR_RESOLVED,
			&cm_event);
	if (ret) {
		rdma_error("Failed to receive a valid event, ret = %d \n", ret);
		return ret;
	}
	/* we ack the event */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
		return -errno;
	}
	debug("RDMA address is resolved \n");
	/* Resolves an RDMA route to the destination address in order to
	 * establish a connection */
	ret = rdma_resolve_route(cm_client_id, 2000);
	if (ret) {
		rdma_error("Failed to resolve route, erno: %d \n", -errno);
	       return -errno;
	}
	debug("waiting for cm event: RDMA_CM_EVENT_ROUTE_RESOLVED\n");
	ret = process_rdma_cm_event(cm_event_channel,
			RDMA_CM_EVENT_ROUTE_RESOLVED,
			&cm_event);
	if (ret) {
		rdma_error("Failed to receive a valid event, ret = %d \n", ret);
		return ret;
	}
	/* we ack the event */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
		return -errno;
	}
	printf("Trying to connect to server at : %s port: %d \n",
			inet_ntoa(s_addr->sin_addr),
			ntohs(s_addr->sin_port));
	/* Protection Domain (PD) is similar to a "process abstraction"
	 * in the operating system. All resources are tied to a particular PD.
	 * And accessing recourses across PD will result in a protection fault. */
	pd = ibv_alloc_pd(cm_client_id->verbs);
	if (!pd) {
		rdma_error("Failed to alloc pd, errno: %d \n", -errno);
		return -errno;
	}
	debug("pd allocated at %p \n", pd);
	/* Now we need a completion channel, were the I/O completion
	 * notifications are sent. Remember, this is different from connection
	 * management (CM) event notifications.
	 * A completion channel is also tied to an RDMA device, hence we will
	 * use cm_client_id->verbs. */
	io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
	if (!io_completion_channel) {
		rdma_error("Failed to create IO completion event channel, errno: %d\n",
			       -errno);
	return -errno;
	}
	debug("completion event channel created at : %p \n", io_completion_channel);
	/* Now we create a completion queue (CQ) where actual I/O
	 * completion metadata is placed. The metadata is packed into a structure
	 * called struct ibv_wc (wc = work completion). ibv_wc has detailed
	 * information about the work completion. An I/O request in RDMA world
	 * is called "work" ;) */
	client_cq = ibv_create_cq(cm_client_id->verbs /* which device*/,
			CQ_CAPACITY /* maximum capacity*/,
			NULL /* user context, not used here */,
			io_completion_channel /* which IO completion channel */,
			0 /* signaling vector, not used here*/);
	if (!client_cq) {
		rdma_error("Failed to create CQ, errno: %d \n", -errno);
		return -errno;
	}
	debug("CQ created at %p with %d elements \n", client_cq, client_cq->cqe);
	ret = ibv_req_notify_cq(client_cq, 0);
	if (ret) {
		rdma_error("Failed to request notifications, errno: %d\n", -errno);
		return -errno;
	}
    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
     * The capacity here is define statically but this can be probed from the
	 * device. We just use a small number as defined in rdma_common.h */
       bzero(&qp_init_attr, sizeof qp_init_attr);
       qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
       qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
       qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
       qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
       qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
       /* We use same completion queue, but one can use different queues */
       qp_init_attr.recv_cq = client_cq; /* Where should I notify for receive completion operations */
       qp_init_attr.send_cq = client_cq; /* Where should I notify for send completion operations */
       /*Lets create a QP */
       ret = rdma_create_qp(cm_client_id /* which connection id */,
		       pd /* which protection domain*/,
		       &qp_init_attr /* Initial attributes */);
	if (ret) {
		rdma_error("Failed to create QP, errno: %d \n", -errno);
	       return -errno;
	}
	client_qp = cm_client_id->qp;
	debug("QP created at %p \n", client_qp);
	return 0;
}



/* Pre-posts a receive buffer before calling rdma_connect () */
static int client_pre_post_recv_buffer()
{
	int ret = -1;
	server_metadata_mr = rdma_buffer_register(pd,
			&server_metadata_attr,
			sizeof(server_metadata_attr),
			(IBV_ACCESS_LOCAL_WRITE));
	if(!server_metadata_mr){
		rdma_error("Failed to setup the server metadata mr , -ENOMEM\n");
		return -ENOMEM;
	}
	server_recv_sge.addr = (uint64_t) server_metadata_mr->addr;
	server_recv_sge.length = (uint32_t) server_metadata_mr->length;
	server_recv_sge.lkey = (uint32_t) server_metadata_mr->lkey;
	/* now we link it to the request */
	bzero(&server_recv_wr, sizeof(server_recv_wr));
	server_recv_wr.sg_list = &server_recv_sge;
	server_recv_wr.num_sge = 1;
	ret = ibv_post_recv(client_qp /* which QP */,
		      &server_recv_wr /* receive work request*/,
		      &bad_server_recv_wr /* error WRs */);
	if (ret) {
		rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
		return ret;
	}
	debug("Receive buffer pre-posting is successful \n");
	return 0;
}



/* Connects to the RDMA server */
static int client_connect_to_server()
{
	struct rdma_conn_param conn_param;
	struct rdma_cm_event *cm_event = NULL;
	int ret = -1;
	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 3;
	conn_param.responder_resources = 3;
	conn_param.retry_count = 3; // if fail, then how many times to retry
	ret = rdma_connect(cm_client_id, &conn_param);
	if (ret) {
		rdma_error("Failed to connect to remote host , errno: %d\n", -errno);
		return -errno;
	}
	debug("waiting for cm event: RDMA_CM_EVENT_ESTABLISHED\n");
	ret = process_rdma_cm_event(cm_event_channel,
			RDMA_CM_EVENT_ESTABLISHED,
			&cm_event);
	if (ret) {
		rdma_error("Failed to get cm event, ret = %d \n", ret);
	       return ret;
	}
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge cm event, errno: %d\n",
			       -errno);
		return -errno;
	}
	printf("The client is connected successfully \n");
	return 0;
}



/* Send client side src buffer metadata to the server. This metadata on
 * the server side is unused. This is shown for the illustration purpose. */
static int client_send_metadata_to_server()
{
	int ret = -1;

	// Allocate buffer to be used by client for RDMA.
	client_buffer_mr = rdma_buffer_alloc_gpu(pd,
			TOTALSIZE,
			(IBV_ACCESS_LOCAL_WRITE|
			 IBV_ACCESS_REMOTE_READ|
			 IBV_ACCESS_REMOTE_WRITE));		
	/* we prepare metadata for the first buffer */
	client_metadata_attr.address = (uint64_t) client_buffer_mr->addr;
	client_metadata_attr.length = client_buffer_mr->length;
	client_metadata_attr.stag.local_stag = client_buffer_mr->lkey;
	/* now we register the metadata memory */
	client_metadata_mr = rdma_buffer_register(pd,
			&client_metadata_attr,
			sizeof(client_metadata_attr),
			IBV_ACCESS_LOCAL_WRITE);
	if(!client_metadata_mr) {
		rdma_error("Failed to register the client metadata buffer, ret = %d \n", ret);
		return ret;
	}
	/* now we fill up SGE */
	client_send_sge.addr = (uint64_t) client_metadata_mr->addr;
	client_send_sge.length = (uint32_t) client_metadata_mr->length;
	client_send_sge.lkey = client_metadata_mr->lkey;
	/* now we link to the send work request */
	bzero(&client_send_wr, sizeof(client_send_wr));
	client_send_wr.sg_list = &client_send_sge;
	client_send_wr.num_sge = 1;
	client_send_wr.opcode = IBV_WR_SEND;
	client_send_wr.send_flags = IBV_SEND_SIGNALED;
	/* Now we post it */
	ret = ibv_post_send(client_qp,
		       &client_send_wr,
	       &bad_client_send_wr);
	if (ret) {
		rdma_error("Failed to send client metadata, errno: %d \n",
				-ret);
		return -ret;
	}

	return ret;
}
//Await a CQ
int wait_recv_comp()
{
	struct ibv_wc wc;
	int ret;
	printf("*\n\n");

	struct ibv_cq *evt_cq = NULL;

	void *cq_context = NULL;


	ret = ibv_get_cq_event(io_completion_channel, &evt_cq, &cq_context);
	if (ret){
		rdma_error("Failed to get next CQ event due to %d \n", -errno);
		return -errno;
	}
	ret = ibv_req_notify_cq(evt_cq, 0);
	if (ret)
		return 1;

	ibv_ack_cq_events(evt_cq,1);

	do {
		fprintf(stderr, "Found: %d Cs\n", ret);

               ret = ibv_poll_cq(client_cq, 1, &wc);
               if (ret < 0) {
                       fprintf(stderr, "Failed to poll completions from the CQ\n");
                       return 1;
               }

               /* there may be an extra event with no completion in the CQ */
               if (ret == 0){
                       fprintf(stderr, "No events left\n");
                       continue;
			   }
               if (wc.status != IBV_WC_SUCCESS) {
                       fprintf(stderr, "Completion with status 0x%x was found\n", wc.status);
                       return 1;
               }
       } while (ret);

	printf("recv.5. %d\n", ret);

	return 0;
}


/* This function starts GPU function. */
static void client_write_data()
{
	//This might be wrong. Perhaps you get the event at the place you send it. In that case
	// I need to checkserverside if it's done.
	//wait_recv_comp();
	write_rdma_buffer();
}

/* This function disconnects the RDMA connection from the server and cleans up
 * all the resources. */
static int client_disconnect_and_clean()
{
	struct rdma_cm_event *cm_event = NULL;
	int ret = -1;

	/* Now we wait for the server to send us disconnect event */
	debug("Waiting for cm event: RDMA_CM_EVENT_DISCONNECTED\n");
	ret = process_rdma_cm_event(cm_event_channel,
		   RDMA_CM_EVENT_DISCONNECTED,
		   &cm_event);
	if (ret) {
	   rdma_error("Failed to get disconnect event, ret = %d \n", ret);
	   return ret;
	}

	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge cm event, errno: %d\n",
			       -errno);
		//continuing anyways
	}
	printf("A disconnect event is received from the server...\n");
	/* We free all the resources */
	/* Destroy QP */
	rdma_destroy_qp(cm_client_id);
	/* Destroy client cm id */
	ret = rdma_destroy_id(cm_client_id);
	if (ret) {
		rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
		// we continue anyways;
	}
	/* Destroy CQ */
	ret = ibv_destroy_cq(client_cq);
	if (ret) {
		rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
		// we continue anyways;
	}
	/* Destroy completion channel */
	ret = ibv_destroy_comp_channel(io_completion_channel);
	if (ret) {
		rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
		// we continue anyways;
	}
	/* Destroy memory buffers */
	rdma_buffer_free_gpu();
	rdma_buffer_deregister(server_metadata_mr);
	rdma_buffer_deregister(client_metadata_mr);

	/* Destroy protection domain */
	ret = ibv_dealloc_pd(pd);
	if (ret) {
		rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
		// we continue anyways;
	}
	rdma_destroy_event_channel(cm_event_channel);
	printf("Client resource clean up is complete \n");
	return 0;
}

void usage() {
	printf("Usage:\n");
	printf("rdma_client: [-a <server_addr>] [-p <server_port>]\n");
	printf("(default IP is 127.0.0.1 and port is %d)\n", DEFAULT_RDMA_PORT);
	exit(1);
}

int main(int argc, char **argv) {
	int ret, option;
	struct sockaddr_in server_sockaddr;
	bzero(&server_sockaddr, sizeof server_sockaddr);
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	/* Parse Command Line Arguments */
	while ((option = getopt(argc, argv, "a:p:")) != -1) {
		switch (option) {
			case 'a':
				/* remember, this overwrites the port info */
				ret = get_addr(optarg, (struct sockaddr*) &server_sockaddr);
				if (ret) {
					rdma_error("Invalid IP \n");
					return ret;
				}
				break;
			case 'p':
				/* passed port to listen on */
				server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0));
				break;
			default:
				usage();
				break;
			}
		}
	if (!server_sockaddr.sin_port) {
	  /* no port provided, use the default port */
	  server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT);
	  }
	ret = client_prepare_connection(&server_sockaddr);
	if (ret) {
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	 }
	ret = client_pre_post_recv_buffer();
	if (ret) {
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	ret = client_connect_to_server();
	if (ret) {
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	ret = client_send_metadata_to_server();
	if (ret) {
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}

	/* Start kernel on GPU */
	client_write_data();
	printf("Reaches1\n");

	
	ret = client_disconnect_and_clean();
	if (ret) {
		rdma_error("Failed to cleanly disconnect and clean up resources \n");
	}
	printf("Reaches2\n");
	return ret;
}
