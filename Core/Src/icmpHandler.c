#include "headers.h"
#include "main.h"






static void SendICMPechoReply(uint8_t index)
{
	uint8_t TXind = GetTxBuff();
	if(TXind < 4)
	{

		memcpy(&TXbuffer[TXind][0], &RXbuffer[index][0], sizeof(RXbuffer[index]) );

		ETHFrame *eth = (ETHFrame *)&TXbuffer[TXind][0];
		IPv4header *ip = (IPv4header *)((uint8_t*)eth + 14);
		uint8_t ipHl = (ip->Ver_IHL & 0b00001111) * 4;
		ICMP *icmp = (ICMP *)((uint8_t*)ip + ipHl);

		eth->dest_add = eth->src_add;
		eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;



		ip->destination = ip->source;
		ip->source = tempss.IPv4_add;
		icmp->type = 0;
		icmp->checksum = 0;
		uint16_t fullLen = htons(ip->TotalLenght) + sizeof(ETHFrame);
		Transmit( fullLen , TXind );

	}


}




void icmpHandler(uint8_t *RXframe, uint8_t index)
{

	ICMP *icmp = (ICMP *) RXframe;

	switch(icmp->type)
	{

		case 8:
			SendICMPechoReply(index);
		break;
		default:
			break;

	}



}
