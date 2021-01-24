DPDK      ?= /opt/dpdk-2011
CFLAGS     := $(CFLAGS) -I$(DPDK)/include -I$(DPDK)/include/dpdk -O3  -march=native -Wall  -DALLOW_EXPERIMENTAL_API
LD_FLAGS = -L$(DPDK)/lib64  -Wl,--whole-archive -lrte_net_ixgbe -lrte_net_e1000 -lrte_mempool_ring -Wl,--no-whole-archive
LD_FLAGS := ${LD_FLAGS} -lrte_mempool -lrte_ring -lrte_ethdev -lrte_mbuf -lrte_net -lrte_meter -lrte_lpm -lrte_timer -lrte_hash -lrte_cmdline
LD_FLAGS := ${LD_FLAGS} -lrte_telemetry
LD_FLAGS := ${LD_FLAGS} -lrte_bus_vdev
LD_FLAGS := ${LD_FLAGS} -lrte_rcu
LD_FLAGS := ${LD_FLAGS} -lrte_security
LD_FLAGS := ${LD_FLAGS} -lrte_cryptodev
LD_FLAGS := ${LD_FLAGS} -lrte_eal -lrte_kvargs -lrte_bus_pci -lrte_pci 
LD_FLAGS := ${LD_FLAGS}  -lnuma -pthread -ldl

dispatcher_zc:  dispatcher_zc.o
	gcc -o $@ $^ ${LD_FLAGS} 

client:  client.o
	gcc -o $@ $^ ${LD_FLAGS} 

.PHONY: clean
clean: 
	rm *.o client dispatcher_zc -rf
