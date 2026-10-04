#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_


#define APP_ETH_RX_BUFFER_SIZE         1536U
#define APP_ETH_TX_BUFFER_SIZE         2048U
#define APP_OPENFLOW_RX_BUFFER_SIZE    2048U
#define APP_CONTROLLER_PORT            6653U

#define APP_DEVICE_IPV4_ADDRESS        {192U, 168U, 1U, 4U}
#define APP_DEVICE_MAC_ADDRESS         {0x00U, 0x80U, 0xE1U, 0x00U, 0x00U, 0x00U}



#define APP_ARP_CACHE_CAPACITY         128U
#define APP_ARP_AGING_TIMEOUT_TICKS    300U
#define APP_ARP_REQUEST_INTERVAL_TICKS 5U

#define APP_MAC_TABLE_CAPACITY         256U
#define APP_MAC_AGING_TIMEOUT_TICKS    300U


#define APP_SWITCH_CAPACITY            32U
#define APP_SWITCH_PORT_CAPACITY       256U
#define APP_MAX_PATH_LENGTH            32U
#define APP_LLDP_AGING_TIMEOUT_TICKS   600U



#define APP_TCP_CONNECTION_CAPACITY    40U
#define APP_TCP_MAX_RETRIES            5U
#define APP_TCP_INACTIVITY_TIMEOUT_TICKS 300U
#define APP_TCP_MSS                    1460U
#define APP_TCP_WINDOW_SIZE            APP_OPENFLOW_RX_BUFFER_SIZE
#define APP_PACKET_OUT_FIXED_PAYLOAD_SIZE 40U
#define APP_PACKET_OUT_MAX_FRAME_SIZE (APP_TCP_MSS - APP_PACKET_OUT_FIXED_PAYLOAD_SIZE)


#define APP_DWT_SAMPLE_CAPACITY        32U
#define APP_DWT_CHANNELS               14U


#define APP_CONSOLE_BUFFER_CAPACITY        256U
#define APP_CONSOLE_FORMAT_BUFFER_CAPACITY 128U
#define APP_STARTUP_DELAY_MS           3000U


#define APP_AXI_SRAM __attribute__((section(".bss.axisram"), aligned(32)))
#define APP_DTCM_BSS __attribute__((section(".bss.dtcm"), aligned(8)))
#define APP_DWT_BSS __attribute__((section(".dwt_memory"), aligned(32)))

#endif