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
extern void packetHandler(uint8_t *RXframe, uint8_t index);
extern void icmpHandler(uint8_t *RXframe, uint8_t index);
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
	uint8_t rx_index;
	uint16_t total_length;
	ETHFrame   *eth_hdr;
	IPv4header *ip_hdr;
	uint8_t IHL;
	IPv4header *ip;
	void *transport_hdr;
	uint8_t    *payload;

}packetinf;


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
//default tcp table values

    uint32_t local_seq_sent;      // utoljara kkikuldott sajat bajt sorszama
    uint32_t local_seq_acked;    // a remote altal utoljara visszaigazolt sorszam
    uint32_t remote_seq_expected; // a remottol elvart kovetkezo sec (ki kuldott ack)

	//Fast retransmit
	uint32_t last_seen_ack;
	uint8_t D_ack_counter;

	//hardver hiba retrans
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
    uint32_t seconds;
    uint32_t nanoseconds;
} PTP_Time;

void PTP_Init(void);
PTP_Time Get_MAC_Time(void);

extern tempips tempss;
extern const MAC_Addr_t MAC_BROADCAST;
extern const MAC_Addr_t MAC_NULL;
extern void arpreqvest(ipv4 ipv4add);
extern void tcphandler(packetinf *inf);
extern uint8_t getmac(ipv4 ip, MAC_Addr_t *out_mac);

static inline void GenETHHeader(ETHFrame *frame, MAC_Addr_t dest, uint16_t type)
{
	frame->dest_add = dest;
	frame->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
	frame->ETHtype = type;

}






#pragma pack(pop)

void arp(uint8_t *rx_frame);
#endif /* SRC_HEADERS_H_ */
