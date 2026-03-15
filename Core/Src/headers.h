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


extern uint8_t RXbuffer[ETH_RX_DESC_CNT][1536] __attribute__((aligned(32))) __attribute__((section(".RxBufferSection")));
extern uint8_t TXbuffer[ETH_TX_DESC_CNT][1536] __attribute__((aligned(32))) __attribute__((section(".TxBufferSection")));
extern ETH_HandleTypeDef heth;
extern void initarp();
extern void writeline(const char* fmt, ...);
extern void packetHandler(uint8_t *RXframe, uint8_t index);
extern void icmpHandler(uint8_t *RXframe, uint8_t index);
#define htons(x) __builtin_bswap16(x)
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

extern tempips tempss;
extern const MAC_Addr_t MAC_BROADCAST;
extern const MAC_Addr_t MAC_NULL;
extern void arpreqvest(ipv4 ipv4add, uint8_t *TXframe);


static inline void GenETHHeader(ETHFrame *frame, MAC_Addr_t dest, uint16_t type)
{
	frame->dest_add = dest;
	frame->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
	frame->ETHtype = type;

}






#pragma pack(pop)

void arp(uint8_t *rx_frame);
#endif /* SRC_HEADERS_H_ */
