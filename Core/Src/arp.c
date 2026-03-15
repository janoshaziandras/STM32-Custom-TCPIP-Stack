#include "headers.h"
#include "main.h"




arprec arptable[10];
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
			.sender_ip = *(ipv4 *)tempss.IPv4_add.addr,

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
			for(int i = 0; i<10; i++)
			{
				if(arptable[i].state !=0)
				{
					if(memcmp(&arptable[i].ipadd, &arphead->sender_ip, sizeof(ipv4)) == 0)
					{
						if(memcmp(&arptable[i].macadd, &arphead->sender_mac, sizeof(MAC_Addr_t)) == 0 )
						{

						recordExist = 1;
						break;
						}
					else
						{
						arptable[i].macadd = arphead->sender_mac;
						recordExist = 1;
						break;
						}
					}
				}else if(FirstEmpty == -1)
				{
					FirstEmpty = i;
				}
			}
			if(recordExist == 0 && FirstEmpty != -1)
			{
				arptable[FirstEmpty].ipadd = arphead->sender_ip;
				arptable[FirstEmpty].macadd = arphead->sender_mac;
				arptable[FirstEmpty].state = 2;

			}

			    }



void arp(uint8_t *RXframe)
{




	arpheader *arphead = (arpheader *)RXframe;

		if(arphead->Operation == htons(1))
{

	handletable(arphead );
	uint8_t BuffIndex = GetTxBuff();

	if(BuffIndex < ETH_TX_DESC_CNT)
	{

		ARPPacket *pkt = (ARPPacket *)&TXbuffer[BuffIndex][0];
		GenETHHeader(&pkt->eth, arphead->sender_mac, htons(0x0806));
		GenArpreply(&pkt->arp,arphead->sender_mac, arphead->sender_ip);

		Transmit(sizeof(ARPPacket), BuffIndex);



	}
}else if(arphead->Operation == htons(2))
{
	handletable(arphead );


}

}
void arpreqvest(ipv4 ipv4add, uint8_t *TXframe)
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
















