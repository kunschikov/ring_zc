#include <unistd.h>           //sleep
#include <signal.h>           //signal

#ifndef ALLOW_EXPERIMENTAL_API
#define ALLOW_EXPERIMENTAL_API
#endif
#include <rte_ethdev.h>
#define RX_RING_SIZE          512
#define NUM_MBUFS             ((2*1024)-1)
#define MBUF_CACHE_SIZE       512
#define BURST_SIZE            32
#define SCHED_TX_RING_SZ      65536

#define RTE_LOGTYPE_DISTRAPP RTE_LOGTYPE_USER1

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define MAX_RING_COUNT       256 

int ring_count           = 1;
int rss_queue_count      = 2;
int first_ring_number    = 0;
int last_ring_number     = 0;
int first_queue_number   = 0;
int last_queue_number    = 0;
int pool_size            = NUM_MBUFS;
int cache_size           = MBUF_CACHE_SIZE;
int priv_size            = 0;
int data_room_size       = RTE_MBUF_DEFAULT_BUF_SIZE;
uint16_t port            = 0;
int ring_size            = 64*1024;

struct rte_ring *rings[MAX_RING_COUNT] ={NULL};
struct rte_mempool *mbuf_pool;
const char* poolname = "POOL";

void read_environment()
{
     if(getenv("RSS_QUEUE_COUNT"))
          rss_queue_count = atoi(getenv("RSS_QUEUE_COUNT"));
     printf("\nThe 'RSS_QUEUE_COUNT' environment variable sets the rss channel queue count: %d\n",  rss_queue_count);
     last_queue_number = rss_queue_count - 1;
     last_ring_number = ring_count - 1;

     if(getenv("RING_COUNT"))
          ring_count = atoi(getenv("RING_COUNT"));

     first_ring_number = 0;
     if(getenv("FIRST_RING"))
          first_ring_number = atoi(getenv("FIRST_RING"));
     printf("\nthe 'FIRST_RING' environment variable specifies the first output ring to handle by this instance: %d {default: 0}\n", first_ring_number);

     last_ring_number = ring_count - 1;
     if(getenv("LAST_RING"))
          last_ring_number = atoi(getenv("LAST_RING"));
     printf("\nthe 'LAST_RING' environment variable specifies the last output ring to handle by this instance: %d {default: output ring count(%d) - 1 = %d}\n",
               last_ring_number, ring_count, ring_count - 1);

     ring_count = last_ring_number - first_ring_number + 1;

     if(getenv("POOL_SIZE"))
          pool_size = atoi(getenv("POOL_SIZE"));
     printf("\nthe 'POOL_SIZE' environment variable handles membuf: %d elements.\n"
               "The optimum size (in terms of memory usage) for a mempool is when n is a power of two minus one: n = (2^q - 1)\n",
               pool_size);

     if(getenv("CACHE_SIZE"))
          cache_size = atoi(getenv("CACHE_SIZE"));
#define CONFIG_RTE_MEMPOOL_CACHE_MAX_SIZE 512
     printf("\n'CACHE_SIZE' env variable, per-lcore cache size: %d bytes.\n "
               "This argument must be lower or equal to CONFIG_RTE_MEMPOOL_CACHE_MAX_SIZE == %d and  pool_size / 1.5 == %d.\n"
               " It is advised to choose cache_size to have \"POOL_SIZE modulo CACHE_SIZE == 0\": "
               "if this is not the case, some elements will always stay in the pool and will never be used.\n"
               " The access to the per-lcore table is of course faster than the multi-producer/consumer pool.\n"
               " The cache can be disabled if the cache_size argument is set to 0; it can be useful to avoid losing objects in cache)\n",
               cache_size, CONFIG_RTE_MEMPOOL_CACHE_MAX_SIZE, (int)(pool_size/1.5));

     if(getenv("PRIV_SIZE"))
          priv_size = atoi(getenv("PRIV_SIZE"));
     printf("\n'PRIV_SIZE' application private data size: %d bytes.\n "
               "Size of application private are between the rte_mbuf structure and the data buffer. This value must be aligned to RTE_MBUF_PRIV_ALIGN == %d\n",
               priv_size, RTE_MBUF_PRIV_ALIGN);

     if(getenv("ROOM_SIZE"))
          data_room_size = atoi(getenv("ROOM_SIZE"));
     printf("\n'ROOM_SIZE' room size: %d bytes. Size of data buffer in each mbuf, including RTE_PKTMBUF_HEADROOM == %d\n",
               data_room_size, RTE_PKTMBUF_HEADROOM);

     if(getenv("PORT"))
          port = atoi(getenv("PORT"));
     printf("\nthe 'PORT' environment variable specifies the DPDK iface number used by this instance: %d\n", port);

     if(getenv("FIRST_QUEUE"))
          first_queue_number = atoi(getenv("FIRST_QUEUE"));
     printf("\nthe 'FIRST_QUEUE' environment variable specifies the first RSS channel queue to handle by this instance: %d {default: 0}\n", first_queue_number);

     if(getenv("LAST_QUEUE"))
          last_queue_number = atoi(getenv("LAST_QUEUE"));
     printf("\nthe 'LAST_QUEUE' environment variable specifies the last RSS channel queue to handle by this instance: %d {default: RSS_QUEUE_COUNT(%d) - 1 = %d}\n",
               last_queue_number, rss_queue_count, rss_queue_count - 1);

     if(getenv("RING_SIZE"))
          ring_size = atoi(getenv("RING_SIZE"));
     printf("\nThe 'RING_SIZE' enviroment variable sets up size of the each output ring. The size of the ring must be a power of 2: %d\n", ring_size);
}


/**
 * It will be called as the callback for specified port after a LSI interrupt
 * has been fully handled. This callback needs to be implemented carefully as
 * it will be called in the interrupt host thread which is different from the
 * application main thread.
 *
 * @param port_id
 *  Port id.
 * @param type
 *  event type.
 * @param param
 *  Pointer to(address of) the parameters.
 *
 * @return
 *  int.
 */
int lsi_event_callback(uint16_t port_id, enum rte_eth_event_type type, void *param, void *ret_param)
{
     struct rte_eth_link link;
     int ret;

     RTE_SET_USED(param);
     RTE_SET_USED(ret_param);

     printf("\n\nIn registered callback...\n");
     printf("Event type: %s\n", type == RTE_ETH_EVENT_INTR_LSC ? "LSC interrupt" : "unknown event");
     ret = rte_eth_link_get_nowait(port_id, &link);
     if (ret < 0) {
          printf("Failed link get on port %d: %s\n",
                 port_id, rte_strerror(-ret));
          return ret;
     }
     if (link.link_status) {
          printf("Port %d Link Up - speed %u Mbps - %s\n\n",
                    port_id, (unsigned)link.link_speed,
               (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
                    ("full-duplex") : ("half-duplex"));
     } else
          printf("Port %d Link Down\n\n", port_id);

     return 0;
}
/* mask of enabled ports */
volatile uint8_t quit_signal;
long long int processed = 0;
static const struct rte_eth_conf port_conf_default = {
     .rxmode = {
          .mq_mode = ETH_MQ_RX_RSS,
          .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
     },
     .txmode = {
          .mq_mode = ETH_MQ_TX_NONE,
     },
     .rx_adv_conf = {
          .rss_conf = {
               .rss_hf = ETH_RSS_IP | ETH_RSS_UDP |
                    ETH_RSS_TCP | ETH_RSS_SCTP,
          }
     },
};

void print_stats(void);

/*
 * Initialises a given port using global settings and with the rx buffers
 * coming from the mbuf_pool passed as parameter
 */
int port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
     struct rte_eth_conf port_conf = port_conf_default;
     int retval;
     uint16_t q;
     uint16_t nb_rxd = RX_RING_SIZE;
     struct rte_eth_dev_info dev_info;

     if (!rte_eth_dev_is_valid_port(port))
          return -1;

     retval = rte_eth_dev_info_get(port, &dev_info);
     if (retval != 0) {
          printf("Error during getting device (port %u) info: %s\n",
                    port, strerror(-retval));
          return retval;
     }

     if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
          port_conf.txmode.offloads |=
               DEV_TX_OFFLOAD_MBUF_FAST_FREE;

     port_conf.rx_adv_conf.rss_conf.rss_hf &=
          dev_info.flow_type_rss_offloads;
     if (port_conf.rx_adv_conf.rss_conf.rss_hf !=
               port_conf_default.rx_adv_conf.rss_conf.rss_hf) {
          printf("Port %u modified RSS hash function based on hardware support,"
               "requested:%#"PRIx64" configured:%#"PRIx64"\n",
               port,
               port_conf_default.rx_adv_conf.rss_conf.rss_hf,
               port_conf.rx_adv_conf.rss_conf.rss_hf);
     }

     retval = rte_eth_dev_configure(port, rss_queue_count, 0, &port_conf);
     if (retval != 0)
          return retval;

         printf("%s:%d %s() number of rx decriptors before adjust: %d\n", __FILE__, __LINE__, __func__, nb_rxd);
     retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, NULL);
         printf("%s:%d %s() number of rx decriptors after adjust: %d\n", __FILE__, __LINE__, __func__, nb_rxd);
     if (retval != 0)
          return retval;

     rte_eth_dev_callback_register(port, RTE_ETH_EVENT_INTR_LSC, lsi_event_callback, NULL);

     for (q = 0; q < rss_queue_count; q++) {
          retval = rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
          if (retval < 0)
               return retval;
     }

     struct rte_eth_rxq_info rx_qinfo;
     if(!rte_eth_rx_queue_info_get(port, 0, &rx_qinfo))
          printf("\ngot queue #0 port #%d info{scattered_rx: %d, nb_desc: %d, mp: %p mp->size: %d}\n", port, rx_qinfo.scattered_rx, rx_qinfo.nb_desc, rx_qinfo.mp, 
                    (rx_qinfo.mp? rx_qinfo.mp->size : 0));
     else
          printf("\nfailed to get got queue #0  info of port #%d\n", port);

     retval = rte_eth_dev_start(port);
     if (retval < 0)
          return retval;

     struct rte_eth_link link;
     do {
          retval = rte_eth_link_get_nowait(port, &link);
          if (retval < 0) {
               printf("Failed link get (port %u): %s\n",
                    port, rte_strerror(-retval));
               return retval;
          } else if (link.link_status)
               break;

          printf("Waiting for Link up on port %"PRIu16"\n", port);
          sleep(1);
     } while (!link.link_status);

     if (!link.link_status) {
          printf("Link down on port %"PRIu16"\n", port);
          return 0;
     }

     struct rte_ether_addr addr;
     retval = rte_eth_macaddr_get(port, &addr);
     if (retval < 0) {
          printf("Failed to get MAC address (port %u): %s\n",
                    port, rte_strerror(-retval));
          return retval;
     }

     printf("Port %u MAC: %02"PRIx8" %02"PRIx8" %02"PRIx8
               " %02"PRIx8" %02"PRIx8" %02"PRIx8"\n",
               port,
               addr.addr_bytes[0], addr.addr_bytes[1],
               addr.addr_bytes[2], addr.addr_bytes[3],
               addr.addr_bytes[4], addr.addr_bytes[5]);

     retval = rte_eth_promiscuous_enable(port);
     if (retval != 0){
         printf("%s:%d %s(failed to enable sniffing)\n", __FILE__, __LINE__, __func__);
          return retval;
     }

     return 0;
}

void do_packet_forwarding()
{
     int empty = 0;
     int queue;
     for(queue = first_queue_number; queue <= last_queue_number; queue++)
     {
          struct rte_ring_zc_data zcd;
          static int current_ring = 0; 
          if(current_ring > last_ring_number || current_ring < first_ring_number)
               current_ring = first_ring_number;
          struct rte_ring *r = rings[current_ring++];
          unsigned free_space;
          unsigned int ret = rte_ring_enqueue_zc_burst_start(r, BURST_SIZE, &zcd, &free_space);
          if (ret == 0) 
               continue;
          uint16_t nb_rx = rte_eth_rx_burst(port, queue, zcd.ptr1, zcd.n1);
          if (nb_rx == zcd.n1 && BURST_SIZE != zcd.n1)
               nb_rx += rte_eth_rx_burst(port, queue, zcd.ptr2, BURST_SIZE - zcd.n1);
          /* Provide packets to the packet processing cores */
          rte_ring_enqueue_zc_finish(r, nb_rx);

          if (nb_rx == 0){
               empty++;
               if((empty > 1000*1000)){
                    print_stats();
                    empty = 0;
               }
               continue;
          }
         printf("%s:%d %s(nb_rx #2: %d)\n", __FILE__, __LINE__, __func__, nb_rx);
          empty = 0;
     }
}


void int_handler(int sig_num)
{
     printf("Exiting on signal %d\n", sig_num);
     quit_signal = 1;
}

void print_stats(void)
{
     struct rte_eth_stats eth_stats;
     long total = 0, lost = 0, drop = 0, nomem = 0, i;
     RTE_ETH_FOREACH_DEV(i) {
          rte_eth_stats_get(i, &eth_stats);
          total     += eth_stats.ipackets;
          lost      += eth_stats.imissed;
          drop      += eth_stats.ierrors;
          nomem     += eth_stats.rx_nombuf;
     }

         printf("%s:%d %s(total %lu, lost %lu, drop %lu, nomem %lu) = processed %llu\n", __FILE__, __LINE__, __func__,
                   total, lost, drop, nomem, processed);
}

void* create_pool(int nb_ports)
{
     return rte_pktmbuf_pool_create(poolname, pool_size, cache_size, priv_size, data_room_size, rte_socket_id());
}

void* lookup_pool()
{
     return rte_mempool_lookup(poolname);
}

void create_rings(int nb_ports)
{
     char ring_name[8] = {0};
     int i;


     printf("\nttotal ring count: %d\n", ring_count);
     for(i = 0; i < ring_count; i++){
          sprintf(ring_name, "RING%d", i);
          rings[i] = rte_ring_create(ring_name, ring_size, rte_socket_id(), RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);
          if (rings[i] == NULL)
               rte_exit(EXIT_FAILURE, "Cannot create output ring #%d\n", i);
          printf("created ring #%d named '%s'\n", i, ring_name);
     }

}

void lookup_rings()
{
     char ring_name[8] = {0};
     int i;
     for(i = first_ring_number; i <= last_ring_number; i++){
          sprintf(ring_name, "RING%d", i);
          rings[i] = rte_ring_lookup(ring_name);
          if (rings[i] == NULL)
               rte_exit(EXIT_FAILURE, "failed to lookup output ring #%d\n", i);
          printf("found already created ring #%d named '%s'\n", i, ring_name);
     }

}

void initialize_ports(int nb_ports)
{
     uint16_t portid;
     /* initialize all ports */
     RTE_ETH_FOREACH_DEV(portid) {
          /* init port */
          printf("Initializing port %u... done\n", portid);

          if (port_init(portid, mbuf_pool) != 0)
               rte_exit(EXIT_FAILURE, "Cannot initialize port %u\n",
                         portid);
     }
}


int main(int argc, char *argv[])
{
     unsigned nb_ports;

     /* catch ctrl-c so we can print on exit */
     signal(SIGINT, int_handler);

     /* init EAL */
     int ret = rte_eal_init(argc, argv);
     if (ret < 0)
          rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
     nb_ports = rte_eth_dev_count_avail();
     if (nb_ports == 0)
          rte_exit(EXIT_FAILURE, "Error: no ethernet ports detected\n");

     read_environment();
     if (rte_eal_process_type() == RTE_PROC_PRIMARY){

          mbuf_pool = create_pool(nb_ports);
          if (mbuf_pool == NULL)
               rte_exit(EXIT_FAILURE, "Cannot initialize the mbuf pool\n");
          initialize_ports(nb_ports);
          create_rings(nb_ports);
     }else{
          //mbuf_pool = lookup_pool();
          lookup_rings();
     }
     const int socket_id = rte_socket_id();

     if (rte_eth_dev_socket_id(port) > 0 && rte_eth_dev_socket_id(port) != socket_id)
          printf("\nWARNING, port %u is on remote NUMA node to RX thread."
                    "\n\tPerformance will not be optimal.\n", port);

     printf("\nCore %u doing packet RX.\n", rte_lcore_id());

     while (! quit_signal) 
          do_packet_forwarding();

     print_stats();
     return EXIT_SUCCESS;
}
