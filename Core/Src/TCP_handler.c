#include "headers.h"
#include "main.h"

#define SEQ_GT(a, b)  ((int32_t)((a) - (b)) > 0)
#define SEQ_GEQ(a, b) ((int32_t)((a) - (b)) >= 0)

tcpconnection tcptable[8];

static uint8_t newConnection(void);
static uint8_t getConnindx(TCP *tcp, uint8_t index);
static uint8_t TCP_Handshake( TCP_Handshakeinf HSinf);

void tcphandler(uint8_t index)
{
	TCP *tcp = (TCP *)&RXbuffer[index][inf[index].parsed_offset];
	inf[index].tcp_pointer = tcp;

	uint8_t tcp_hdr_len = ((htons(tcp->doffResFlag) >> 12) & 0x0F) * 4;
	inf[index].parsed_offset += tcp_hdr_len;
	inf[index].payload_pointer = &RXbuffer[index][inf[index].parsed_offset];

	uint8_t Tindex = getConnindx(tcp, index);

	// writeline("Kapcsolat indexe: %d", Tindex);

	if(Tindex == 255 && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_SYN)) // ha nem létezik, es sync akkor új kapcsolat
	{
		uint8_t freeIdx = newConnection();

		if(freeIdx != 255 ) // ha van hely a tablaban
		{
			//  writeline("SYN_RECEIVED");
			//  state machine frissitese es kitoltese es tabla feltoltese

			tcptable[freeIdx].state = SYN_RECEIVED;
			memcpy(tcptable[freeIdx].remote_ip.addr, inf[index].ip_pointer->source.addr, 4);
			tcptable[freeIdx].remote_port = htons(tcp->srcPort);
			tcptable[freeIdx].local_port = htons(tcp->desPort);

			tcptable[freeIdx].remote_seq_expected = (htonl(tcp->seqNumber)) + 1;
			tcptable[freeIdx].last_seen_ack = htonl(tcp->seqNumber); //fast retranshoz

			//-------------------------------------------------------------------------------

			//            //writeline("--- Uj kapcsolat reszletei ---");
			//writeline("Index: %d", freeIdx);
			// writeline("Forras IP: %d.%d.%d.%d",
			// tcptable[freeIdx].remote_ip.addr[0],
			//tcptable[freeIdx].remote_ip.addr[1],
			//tcptable[freeIdx].remote_ip.addr[2],
			//tcptable[freeIdx].remote_ip.addr[3]);
			//writeline("Forras Port: %d", tcptable[freeIdx].local_port);

			//writeline("State: %d", tcptable[freeIdx].state);
			// writeline("------------------------------");

			TCP_Handshakeinf Hinf;
			Hinf.data_len = htons(inf[index].ip_pointer->TotalLenght) - (inf[index].ip_pointer->Ver_IHL & 0x0F) * 4 - tcp_hdr_len;
			Hinf.flags = TCP_FLAG_ACK | TCP_FLAG_SYN;
			Hinf.indx = freeIdx;
			uint8_t err = TCP_Handshake(Hinf); // call execute function

			if(err == 255)
			{
				tcptable[freeIdx].hskaeinf = Hinf;
				tcptable[freeIdx].errstate = 255;

			}

		}
	}else if(Tindex != 255)// létezo kapcsolat.
	{
		// --- BEJÖVŐ CSOMAG LOGOLÁSA ---

		writeline("state: %d ", tcptable[Tindex].state);
		// ------------------------------

		uint32_t recived_seq = htonl(tcp->seqNumber);
		uint32_t recived_ack = htonl(tcp->ackNumber);

		uint8_t ip_hdr_len = (inf[index].ip_pointer->Ver_IHL & 0x0F) * 4;
		uint16_t payload_len = htons(inf[index].ip_pointer->TotalLenght) - ip_hdr_len - tcp_hdr_len;
		if(GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_SYN) || GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_FIN))
		{
			payload_len += 1;
		}

		//global vars letezo kapcs eseten
		//----------------------------------
		tcptable[Tindex].last_seen_ack = tcptable[Tindex].local_seq_acked;
		tcptable[Tindex].local_seq_acked = recived_ack;

		//retransmit logic duplicated ack esetére

		for(int i = 0; i< ETH_TX_DESC_CNT; i++)
		{
			if(  GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_ACK)  &&  tcp_buff_lock[i].conn_idx == Tindex && tcp_buff_lock[i].is_locked == 1)
			{
				if(SEQ_GT(recived_ack, tcp_buff_lock[i].seq_num))
				{
					tcp_buff_lock[i].is_locked = 0;
					tcp_buff_lock[i].retry_count = 0;//buff_lock feloldasa.

				}else{

					if(tcptable[Tindex].last_seen_ack == recived_ack)
					{
						if(tcptable[Tindex].D_ack_counter >3)
						{
							if(tcp_buff_lock[i].retry_count < 3)
							{
								Transmit(tcp_buff_lock[i].data_len ,i); //retransmit on dublicated ack
								writeline("fast retrasmit");
								tcp_buff_lock[i].retry_count++;

								tcptable[Tindex].D_ack_counter = 0;
							}else{

								tcptable[Tindex].state = CLOSED;
								return;
							}
						}
						tcptable[Tindex].D_ack_counter++;

					}
				}
				writeline("infos: %d  ack: %d seq: %d Dackcounter: %d ",Tindex , htons(tcp->ackNumber), tcp_buff_lock[i].seq_num, tcptable[Tindex].D_ack_counter++);

			}

		}
		//retransmit logic duplicated ack esetére
		//-----------------------------------------

		//State machine allapotok kezelese














		if(tcptable[Tindex].state == ESTABLISHED)
		{




		}

























		if(tcptable[Tindex].state == SYN_RECEIVED && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_ACK))
		{
			writeline("handshakje ack");
			if ( recived_ack == (tcptable[Tindex].local_seq_sent + 1))
			{
				tcptable[Tindex].state = ESTABLISHED;

				writeline("Kezfogas sikeres! Kapcsolat ESTABLISHED. Index: %d", Tindex);
			}
			return;
		}

		if(tcptable[Tindex].state == ESTABLISHED && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_FIN))
		{ writeline("FIN recived");
		tcptable[Tindex].state = CLOSE_WAIT;

		tcptable[Tindex].local_seq_acked = recived_seq;

		TCP_Handshakeinf Hinf;
		Hinf.data_len = payload_len;
		Hinf.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
		Hinf.indx = Tindex;
		uint8_t err = TCP_Handshake(Hinf);

		writeline("FIN csomag erkezett. Allapot: CLOSE_WAIT -> LAST_ACK. Index: %d elkuldes allapota %d", Tindex, err);
		return;
		}

		if(tcptable[Tindex].state == CLOSE_WAIT && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_ACK))
		{

			writeline("Vegso ACK erkezett, kapcsolat lezarva. Index: %d", Tindex);
			tcptable[Tindex].state = CLOSED;

			return;
		}

		if(tcptable[Tindex].state == SYN_RECEIVED && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_SYN))
		{
			writeline("A Linux ujrakuldte a SYN-t, valaszolunk ujra!");

			TCP_Handshake(tcptable[Tindex].hskaeinf);
			return;
		}

		//State machine állapotok kezelése
		//--------------------------------
	}

}

void TCP_clean_table()
{

}

static uint8_t newConnection(void)
{
	for(int i = 0; i < 8; i++)
	{
		if(tcptable[i].state == CLOSED)
		{
			// writeline("ujkapcsolat hozza adva %d",i);
			return i;
		}
	}
	//writeline("tabla megtelt");
	return 255;
}

static uint8_t getConnindx(TCP *tcp, uint8_t index)
{
	for(int i = 0; i < 8; i++)
	{
		if(tcptable[i].state != CLOSED)
		{
			if (*(uint32_t*)&tcptable[i].remote_ip == *(uint32_t*)&inf[index].ip_pointer->source)
			{
				if(htons(tcp->srcPort) == tcptable[i].remote_port &&
						htons(tcp->desPort) == tcptable[i].local_port)
				{
					return i;
				}
			}
		}
	}
	return 255;
}

void TCP_retrans()
{
	for(int i = 0 ; i < ETH_TX_DESC_CNT; i++)
	{
		if(tcp_buff_lock[i].is_locked == 1 )
		{
			if(SEQ_GEQ(tcp_buff_lock[i].seq_num, tcptable[tcp_buff_lock[i].conn_idx].local_seq_acked) && SEQ_GT(tim6time, tcp_buff_lock[i].sent_time))
			{
				if(tcp_buff_lock[i].retry_count < 3)
				{
				Transmit(tcp_buff_lock[i].data_len,i);
				tcp_buff_lock[i].retry_count++;
				} else
				{
					tcp_buff_lock[i].is_locked = 0;
					tcptable[tcp_buff_lock[i].conn_idx].state = CLOSED;

				}

			}

		}

	}

	for(int i = 0; i < 8 ; i++){

		if(tcptable[i].errstate == 255 && tcptable[i].state != CLOSED) //arp fail retry
		{

			uint8_t err =  TCP_Handshake(tcptable[i].hskaeinf);

			if(err == 255 )
			{
				if(tcptable[i].D_ack_counter >= 10)
				{
					tcptable[i].state = CLOSED;

				}else{

					tcptable[i].D_ack_counter++;

				}
			}else if(err == 0)
			{
				tcptable[i].errstate = 0;

			}

		}

	}
}

static uint8_t TCP_Handshake( TCP_Handshakeinf HSinf)
{
	//ETH HEADER-----------------------------------
	uint8_t buf_idx = GetTxBuff();

	if(buf_idx == 255)
	{
		writeline("arp kikuldes");
		return 255;
	}

	uint8_t *tx_ptr = TXbuffer[buf_idx];

	ETHFrame   *tx_eth = (ETHFrame *)tx_ptr;

	uint8_t succ = getmac(tcptable[HSinf.indx].remote_ip, &tx_eth->dest_add);
	if(succ == 0) return 255;

	tx_eth->ETHtype = htons(0x0800);
	tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
	//IP HEADER------------------------------------------------------------

	IPv4header *tx_ip  = (IPv4header *)(tx_ptr + sizeof(ETHFrame));

	tx_ip->source = tempss.IPv4_add;
	tx_ip->destination = tcptable[HSinf.indx].remote_ip;
	tx_ip->Ver_IHL = 0x45;
	tx_ip->ToS = 0;
	tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP));
	tx_ip->Identification = htons(100);
	tx_ip->Flag_Fregmantoff = htons(0x4000);
	tx_ip->TimeToLive = 64;
	tx_ip->Protocol = 6;
	//TCP HEDAER---------------------------------------------------------------

	TCP *tx_tcp = (TCP *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header));

	tx_tcp->desPort = htons(tcptable[HSinf.indx].remote_port);
	tx_tcp->srcPort = htons(tcptable[HSinf.indx].local_port);

	uint16_t doff = sizeof(TCP) / 4;

	tx_tcp->doffResFlag = htons((doff << 12) | (000 << 9) | (HSinf.flags));

	if(HSinf.flags == 0X12)  //if sin_ack
			{

		uint32_t ran = 0;
		HAL_RNG_GenerateRandomNumber(&hrng, &ran);

		tcptable[HSinf.indx].local_seq_sent = ran;

		tx_tcp->seqNumber = htonl(tcptable[HSinf.indx].local_seq_sent);

		tx_tcp->ackNumber = htonl(tcptable[HSinf.indx].remote_seq_expected);

			}else if (HSinf.flags == 0x11)
			{
				tcptable[HSinf.indx].local_seq_sent++;
				tx_tcp->seqNumber = htonl(tcptable[HSinf.indx].local_seq_sent);

				tcptable[HSinf.indx].remote_seq_expected += HSinf.data_len;
				tx_tcp->ackNumber = htonl(tcptable[HSinf.indx].remote_seq_expected);

			}

	tx_tcp->urgentPointer = 0;
	tx_tcp->windowSize = htons(24000);

	tcp_buff_lock[buf_idx].is_locked = 1;
	tcp_buff_lock[buf_idx].seq_num =  tcptable[HSinf.indx].local_seq_sent;
	tcp_buff_lock[buf_idx].conn_idx = HSinf.indx;

	tcp_buff_lock[buf_idx].data_len = sizeof(IPv4header) + sizeof(TCP) + sizeof(ETHFrame);

	Transmit(tcp_buff_lock[buf_idx].data_len , buf_idx);
	tcp_buff_lock[buf_idx].sent_time = tim6time + 2; // time resatrt
	return 0;
}
