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



extern ETH_HandleTypeDef heth;

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
	ETHFrame frame;
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

extern tempips tempss;
extern const MAC_Addr_t MAC_BROADCAST;
extern const MAC_Addr_t MAC_NULL;

static inline void Arpreply(arpheader *arp, MAC_Addr_t target_mac, ipv4 tgip)
{
	arp->frame.src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
	arp->frame.dest_add = target_mac;
	arp->frame.ETHtype = htons(0x0806) ;
	arp->Hardwer_Type= htons(1); //ethernet
	arp->Protocol_Type= htons(0x0800); // ipv4
	arp->Hardware_Length = 6;
	arp->Protocol_Length = 4;
	arp->Operation = htons(2); // reply
	arp->sender_mac = *(MAC_Addr_t *)heth.Init.MACAddr;
	arp->sender_ip = tempss.IPv4_add;
	arp->target_mac = target_mac;
	arp->target_ip =  tgip;


}
static inline void ArpReq(arpheader *arp, ipv4 tgip)
{
	arp->frame.src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
	arp->frame.dest_add = MAC_BROADCAST;
	arp->frame.ETHtype = htons(0x0806) ;
	arp->Hardwer_Type= htons(1); //ethernet
	arp->Protocol_Type= htons(0x0800); // ipv4
	arp->Hardware_Length = 6;
	arp->Protocol_Length = 4;
	arp->Operation = htons(1); // reqwest
	arp->sender_mac = *(MAC_Addr_t *)heth.Init.MACAddr;
	arp->sender_ip = tempss.IPv4_add;
	arp->target_mac = MAC_NULL;
	arp->target_ip =  tgip;

}
#pragma pack(pop)

void arp(uint8_t *rx_frame);
#endif /* SRC_HEADERS_H_ */
