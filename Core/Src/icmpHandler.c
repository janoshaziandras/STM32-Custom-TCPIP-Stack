#include "headers.h"
#include "main.h"

#include <string.h>

static void SendICMPechoReply(uint8_t index)
{
    uint8_t TXind = GetTxBuff();
    if(TXind < ETH_TX_DESC_CNT)
    {
        IPv4header *rx_ip = inf[index].ip_pointer;
        uint16_t original_ip_len = htons(rx_ip->TotalLenght);
        uint16_t ip_header_len = (rx_ip->Ver_IHL & 0x0FU) * 4U;
        if (ip_header_len > original_ip_len || original_ip_len - ip_header_len < sizeof(ICMP)) {
            return;
        }
        uint16_t fullLen = original_ip_len + sizeof(ETHFrame);


        memcpy(&TXbuffer[TXind][0], &RXbuffer[index][0], fullLen);

        ETHFrame *eth = (ETHFrame *)&TXbuffer[TXind][0];
        IPv4header *ip = (IPv4header *)&TXbuffer[TXind][sizeof(ETHFrame)];
        ICMP *icmp = (ICMP *)&TXbuffer[TXind][inf[index].parsed_offset];


        eth->dest_add = eth->src_add;
        eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;


        ip->destination = ip->source;
        ip->source = tempss.IPv4_add;


        icmp->type = 0;
        icmp->checksum = 0;

        Transmit( fullLen , TXind );
    }
}

void icmpHandler(uint8_t index)
{
    ICMP *icmp = (ICMP *)&RXbuffer[index][inf[index].parsed_offset];

    switch(icmp->type)
    {
        case 8:
            SendICMPechoReply(index);
        break;
        default:
            break;
    }
}
