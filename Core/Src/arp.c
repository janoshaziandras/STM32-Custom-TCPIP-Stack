#include "headers.h"
#include "main.h"
#include <stdbool.h>

arprec arptable[10];




void arp(uint8_t *RXframe)
{

	arpheader *arphead = (arpheader *)RXframe;
	if(arphead->Operation == htons(1))
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

	uint8_t BuffIndex = GetTxBuff();

	if(BuffIndex < ETH_TX_DESC_CNT)
	{
		arpheader *send = (arpheader *)&TXbuffer[BuffIndex][0];;
		Arpreply(send, arphead->sender_mac, arphead->sender_ip);
		Transmit(sizeof(arpheader), BuffIndex);



	}
}
void arpreq(ipv4 ipv4add, uint8_t *TXframe)
{
	arpheader *send = (arpheader *)TXframe;
	ArpReq(send, ipv4add);
	static ETH_BufferTypeDef tx_buffer;
		            tx_buffer.buffer = (uint8_t *)send;
		            tx_buffer.len = 64;
		            tx_buffer.next = NULL;

		            TxConfig.Length = 64;
		            TxConfig.TxBuffer = &tx_buffer;
		            TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
		            TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;



		            HAL_StatusTypeDef tx_status = HAL_ETH_Transmit_IT(&heth, &TxConfig);

		            char dbg[50];
		            sprintf(dbg, " -ARP send TX Status: %d\r\n", tx_status);
		            HAL_UART_Transmit(&huart3, (uint8_t*)dbg, strlen(dbg), 100);




}







