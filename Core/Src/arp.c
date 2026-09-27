#include "headers.h"
#include "main.h"

#define ARP_TABLE_SIZE 10
#define ARP_AGING_TIMEOUT_SEC 300

arprec arptable[ARP_TABLE_SIZE];
arpheader arpbase;

void initarp()
{
    arpbase = (arpheader){
        .Hardwer_Type = htons(1),
        .Protocol_Type = htons(0x0800),
        .Hardware_Length = 6,
        .Protocol_Length = 4,
        .Operation = htons(2),
        .sender_mac = *(MAC_Addr_t *)heth.Init.MACAddr,
        .sender_ip = tempss.IPv4_add,
        .target_mac = MAC_BROADCAST,
        .target_ip = {{0x00, 0x00, 0x00, 0x00}}
    };
}

typedef struct {
    ETHFrame eth;
    arpheader arp;
} ARPPacket;

static void GenArpreply(arpheader *arp, MAC_Addr_t target_mac, ipv4 tgip)
{
    *arp = arpbase;
    arp->target_mac = target_mac;
    arp->target_ip = tgip;
}

static void GenArpReq(arpheader *arp, ipv4 tgip)
{
    *arp = arpbase;
    arp->Operation = htons(1);
    arp->target_ip = tgip;
}

void handletable(arpheader *arphead)
{
    bool recordExist = 0;
    int16_t FirstEmpty = -1;

    for(int i = 0; i < ARP_TABLE_SIZE; i++)
    {
        if(arptable[i].state != 0)
        {
            if(memcmp(&arptable[i].ipadd, &arphead->sender_ip, sizeof(ipv4)) == 0)
            {
                arptable[i].macadd = arphead->sender_mac;
                arptable[i].state = 2;
                arptable[i].last_seen = tim6time;
                recordExist = 1;
                break;
            }
        }
        else if(FirstEmpty == -1)
        {
            FirstEmpty = i;
        }
    }

    if(recordExist == 0)
    {
        if(FirstEmpty != -1)
        {
            arptable[FirstEmpty].ipadd = arphead->sender_ip;
            arptable[FirstEmpty].macadd = arphead->sender_mac;
            arptable[FirstEmpty].state = 2;
            arptable[FirstEmpty].last_seen = tim6time;
        }
        else
        {
            arptable[0].ipadd = arphead->sender_ip;
            arptable[0].macadd = arphead->sender_mac;
            arptable[0].state = 2;
            arptable[0].last_seen = tim6time;
        }
    }
}

void arp(uint8_t *RXframe)
{
    arpheader *arphead = (arpheader *)RXframe;

    if(arphead->Operation == htons(1))
    {
        handletable(arphead);
        uint8_t BuffIndex = GetTxBuff();

        if(BuffIndex < ETH_TX_DESC_CNT)
        {
            ARPPacket *pkt = (ARPPacket *)&TXbuffer[BuffIndex][0];
            GenETHHeader(&pkt->eth, arphead->sender_mac, htons(0x0806));
            GenArpreply(&pkt->arp, arphead->sender_mac, arphead->sender_ip);

            Transmit(sizeof(ARPPacket), BuffIndex);
        }
    }
    else if(arphead->Operation == htons(2))
    {
        handletable(arphead);
    }
}

void arpreqvest(ipv4 ipv4add)
{
    uint8_t BuffIndex = GetTxBuff();
    if(BuffIndex < ETH_TX_DESC_CNT)
    {
        ARPPacket *pkt = (ARPPacket *)&TXbuffer[BuffIndex][0];
        GenETHHeader(&pkt->eth, MAC_BROADCAST, htons(0x0806));
        GenArpReq(&pkt->arp, ipv4add);
        Transmit(sizeof(ARPPacket), BuffIndex);
    }
}

uint8_t getmac(ipv4 ip, MAC_Addr_t *out_mac)
{
    static uint32_t last_arp_request_time = 0;

    for(int i = 0; i < ARP_TABLE_SIZE; i++)
    {
        if(arptable[i].state != 0 && memcmp(arptable[i].ipadd.addr, ip.addr, 4) == 0)
        {
            *out_mac = arptable[i].macadd;
            return 1;
        }
    }

    if ((tim6time - last_arp_request_time) > 5)
    {
        arpreqvest(ip);
        last_arp_request_time = tim6time;
    }

    return 0;
}

void arp_aging(void)
{
    for(int i = 0; i < ARP_TABLE_SIZE; i++)
    {
        if(arptable[i].state != 0)
        {
            if((tim6time - arptable[i].last_seen) > ARP_AGING_TIMEOUT_SEC)
            {
                arptable[i].state = 0;
            }
        }
    }
}
