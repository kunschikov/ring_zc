#include <stdbool.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_log.h>
#include "shared_ring_names.h"
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
static const int BURST_SIZE = 256;
int work = true;

void int_handler(int sig_num)
{
     printf("Exiting on signal %d\n", sig_num);
     work = false;
}
int main(int argc, char **argv)
{
     int ret = rte_eal_init(argc, argv);
     if (ret < 0)
          rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

     signal(SIGINT, int_handler);
     struct rte_mbuf *bufs[BURST_SIZE * 4];
     const char* ringname = RING;
     const char* poolname = MBUF_POOL;
     if(getenv("RING"))
          ringname = getenv("RING");
     if(getenv("POOL"))
          poolname = getenv("POOL");
         printf("%s:%d %s() going to work with pool %s and ring %s\n", __FILE__, __LINE__, __func__, poolname, ringname);
     struct rte_ring *ring = rte_ring_lookup(ringname);
     if (ring == NULL)
          rte_exit(EXIT_FAILURE, "Problem getting receiving ring\n");

     if(strlen(poolname)){
          struct rte_mempool *packet_pool = rte_mempool_lookup(poolname);
          if (packet_pool == NULL)
               rte_exit(EXIT_FAILURE, "Problem getting message pool\n");
          printf("%s:%d %s() got mbuf pool named %s\n", __FILE__, __LINE__, __func__, poolname);
     }else
         printf("%s:%d %s()got empty poolname: mbuf pool is disabled\n", __FILE__, __LINE__, __func__);

     long int total = 0;
         printf("%s:%d %s(waiting for packets)\n", __FILE__, __LINE__, __func__);
         int debug = 0;
         if(getenv("DEBUG"))
              debug = 1;
         printf("debug logging is %s. Mode can be switched by setting/unsetting the DEBUG environment variable\n", debug? "ON" : "OFF");
     while(work){
          uint16_t nb_rx = rte_ring_dequeue_burst(ring, (void *)bufs, BURST_SIZE - 1, NULL);
          if (!nb_rx) 
               continue;
          if(debug) printf("%s:%d %s(got %d packets!)\n", __FILE__, __LINE__, __func__, nb_rx);
         int i;
         for(i = 0; i < nb_rx; i++){
              uint8_t* packet = rte_pktmbuf_mtod(bufs[i], void*);
              uint16_t packet_len = rte_pktmbuf_data_len(bufs[i]);
              if(debug) printf("%s:%d %s(got packet %d bytes. [0]: %x, [%d]: %x)\n", __FILE__, __LINE__, __func__, packet_len, packet[0], packet_len, packet[packet_len - 1]);
         }

         total++;
         while (nb_rx--){
              if(debug) printf("%s:%d %s(going to free buf #%d {%p})\n", __FILE__, __LINE__, __func__, nb_rx, bufs[nb_rx]);
              rte_pktmbuf_free(bufs[nb_rx]);
         }
         if(debug) printf("%s:%d %s(processed. Have skipped %d packets!)\n", __FILE__, __LINE__, __func__, nb_rx);
     }
     RTE_LOG(INFO, APP, "Finished packet dump. Got %lu packets.\n", total);
     return 0;
}
