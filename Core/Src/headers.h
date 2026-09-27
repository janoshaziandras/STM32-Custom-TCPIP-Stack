/*
 * headers.h
 *
 * Created on: 2026. febr. 15.
 *
 */

#ifndef SRC_HEADERS_H_
#define SRC_HEADERS_H_
#include "stm32h7xx_hal.h"
#include <stdint.h>
#include "string.h"
#include "stdio.h"



#define CONSOLE_BUF_SIZE 2048
extern volatile uint16_t head;
extern volatile uint16_t tail;
extern char console_buffer[CONSOLE_BUF_SIZE];

extern uint32_t tim6time;

void writeline(const char* fmt, ...);
void console_process(void);
extern uint8_t RXbuffer[ETH_RX_DESC_CNT][1536] __attribute__((aligned(32))) __attribute__((section(".RxBufferSection")));
extern uint8_t TXbuffer[ETH_TX_DESC_CNT][1536] __attribute__((aligned(32))) __attribute__((section(".TxBufferSection")));
extern ETH_HandleTypeDef heth;
extern RNG_HandleTypeDef hrng;
extern void initarp();
extern void arp_aging(void);
extern void packetHandler(uint8_t index);
extern void icmpHandler(uint8_t index);
extern void TCP_retrans();
extern void TCP_clean_table();

#define htons(x) __builtin_bswap16(x)
#define htonl(x) __builtin_bswap32(x)


#pragma pack(push, 1)

typedef struct {
    uint8_t addr[6];
} MAC_Addr_t;

typedef struct {
    uint8_t addr[4];
} ipv4;

typedef struct
{
	MAC_Addr_t dest_add;
	MAC_Addr_t src_add;
	uint16_t ETHtype;

} ETHFrame;

typedef struct
{

	uint16_t Hardwer_Type;
	uint16_t Protocol_Type;
	uint8_t Hardware_Length;
	uint8_t Protocol_Length;
	uint16_t Operation;
	MAC_Addr_t sender_mac;
	ipv4 sender_ip;
	MAC_Addr_t target_mac;
	ipv4 target_ip;

} arpheader;






 typedef struct
 {
	 ipv4 IPv4_add;


 }tempips;

typedef struct
{
	MAC_Addr_t macadd;
	ipv4 ipadd;
	uint8_t  state;
	uint32_t last_seen;




} arprec;

typedef struct
{
	uint8_t Ver_IHL;
	uint8_t ToS;
	uint16_t TotalLenght;
	uint16_t Identification;
	uint16_t Flag_Fregmantoff;
	uint8_t TimeToLive;
	uint8_t Protocol;
	uint16_t HeaderChecksum;
	ipv4 source;
	ipv4 destination;




}IPv4header;

typedef struct
{
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint16_t identifier;
	uint16_t sequenceNumber;



}ICMP;


typedef struct
{
	uint16_t srcPort;
	uint16_t desPort;
	uint32_t seqNumber;
	uint32_t ackNumber;
	uint16_t doffResFlag;
	uint16_t windowSize;
	uint16_t checksum;
	uint16_t urgentPointer;



}TCP;



#define TCP_FLAG_NONE 0x0000
#define TCP_FLAG_FIN  0x0001
#define TCP_FLAG_SYN  0x0002
#define TCP_FLAG_RST  0x0004
#define TCP_FLAG_PSH  0x0008
#define TCP_FLAG_ACK  0x0010
#define TCP_FLAG_URG  0x0020
#define TCP_FLAG_ECE  0x0040
#define TCP_FLAG_CWR  0x0080
#define TCP_FLAG_NS   0x0100


#define GET_TCP_FLAG(raw_doff_res_flags, flag_mask) ((htons(raw_doff_res_flags) & (flag_mask)) != 0)



typedef struct
{

	uint16_t parsed_offset;
	ETHFrame   *eth_pointer;
	IPv4header *ip_pointer;
	TCP *tcp_pointer;
	uint8_t tti;

	uint8_t    *payload_pointer;

	uint8_t bufflock;

}packetinf;

extern packetinf inf[ETH_RX_DESC_CNT];


typedef struct{

uint8_t indx;
uint8_t flags;
uint16_t data_len;



}TCP_Handshakeinf;

typedef struct {
    ipv4 remote_ip;
	uint16_t local_port;
    uint16_t remote_port;
    uint8_t state;
    uint32_t local_seq_sent;
    uint32_t local_seq_acked;
    uint32_t remote_seq_expected;

	uint32_t last_seen_ack;
	uint8_t D_ack_counter;

	uint8_t errstate;
	TCP_Handshakeinf hskaeinf;
	

} tcpconnection;

typedef struct
{
	uint32_t seq_num;
	uint8_t is_locked;
	uint8_t conn_idx;
	uint32_t sent_time;
	uint16_t data_len;
	uint8_t retry_count;
	

} bufferinf;

extern bufferinf tcp_buff_lock[ETH_TX_DESC_CNT];

typedef enum {
    CLOSED = 0,
    LISTEN,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
	CLOSE_WAIT,
	LAST_ACK,
	TIME_WAIT
} TCPstates;


typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t length;
    uint32_t xid;
} ofp_header;


typedef struct {
    uint16_t type;      
    uint16_t length;    
    uint32_t bitmap;    
} ofp_hello_elem_versionbitmap;



typedef struct {

    uint64_t datapath_id;
    uint32_t n_buffers;
    uint8_t  n_tables;
    uint8_t  auxiliary_id;
    uint8_t  pad[2];
    uint32_t capabilities;
    uint32_t reserved;
} ofp_switch_features;



typedef struct{
  
    uint16_t type;
    uint16_t flags;
    uint8_t pad[4];

}ofp_multipart_request;



typedef struct {
    uint32_t port_no;       
    uint8_t  pad[4];        
    uint8_t  hw_addr[6];    
    uint8_t  pad2[2];      
    char     name[16];      
    uint32_t config;        
    uint32_t state;         
    uint32_t curr;         
    uint32_t advertised;    
    uint32_t supported;     
    uint32_t peer;          
    uint32_t curr_speed;    
    uint32_t max_speed;     
}ofp_port;

typedef struct {
    uint8_t reason;    
    uint8_t pad[7];    
    ofp_port desc;     
} ofp_port_status;


typedef struct {
    uint32_t buffer_id;     
    uint16_t total_len;     
    uint8_t  reason;        
    uint8_t  table_id;      
    uint64_t cookie;        
}  ofp_packet_in_header;


typedef struct {
    uint16_t type;
    uint16_t length;
} ofp_match_header;

typedef struct {
    uint32_t oxm_header; 
    uint32_t port_no;    
} oxm_in_port_t;

typedef struct {
    uint32_t seconds;
    uint32_t nanoseconds;
} PTP_Time;

typedef struct {
    uint8_t index;
    uint8_t mac[6];       
    uint32_t port_no;     
    uint8_t is_free;          
} opf_mac_table_entry;


typedef struct {
    uint16_t type;      
    uint16_t len;       
    uint32_t port;      
    uint16_t max_len;   
    uint8_t  pad[6];   
} ofp_action_output;


typedef struct {
    uint32_t buffer_id;   
    uint32_t in_port;     
    uint16_t actions_len; 
    uint8_t  pad[6];
   
} ofp_packet_out;

#define OFPP_FLOOD 0xfffffffb
#define OFP_NO_BUFFER 0xffffffff


#define MAC_TABLE_SIZE 128

typedef struct {
    uint8_t mac[6];     
    uint32_t port_no;
    uint32_t ip_addr;
    uint8_t is_active; 
    uint8_t tcp_index;
} fdb_entry_t;

typedef struct {
    uint64_t cookie;
    uint64_t cookie_mask;
    uint8_t  table_id;
    uint8_t  command;
    uint16_t idle_timeout;
    uint16_t hard_timeout;
    uint16_t priority;
    uint32_t buffer_id;
    uint32_t out_port;
    uint32_t out_group;
    uint16_t flags;
    uint8_t  pad[2];
} ofp_flow_mod;

typedef struct {
    uint16_t type;
    uint16_t len;
    uint8_t  pad[4];
} ofp_instruction_actions;

#define MAX_SWITCHES 8

typedef struct {
    uint8_t active;
    uint8_t neighbor_switch_idx;
    uint32_t neighbor_port;
} link_t;

typedef struct {
    link_t links[64];
} switch_topology_t;


extern switch_topology_t topology[MAX_SWITCHES];

void PTP_Init(void);
PTP_Time Get_MAC_Time(void);

extern tempips tempss;
extern const MAC_Addr_t MAC_BROADCAST;
extern const MAC_Addr_t MAC_NULL;
extern void arpreqvest(ipv4 ipv4add);
extern void tcphandler(uint8_t index);
extern uint8_t getmac(ipv4 ip, MAC_Addr_t *out_mac);
extern void opf_handler(uint8_t index);
extern tcpconnection tcptable[8];
uint8_t sendhello(uint8_t index);
static inline void GenETHHeader(ETHFrame *frame, MAC_Addr_t dest, uint16_t type)

{
	frame->dest_add = dest;
	frame->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
	frame->ETHtype = type;

}






#pragma pack(pop)

void arp(uint8_t *rx_frame);
#endif
