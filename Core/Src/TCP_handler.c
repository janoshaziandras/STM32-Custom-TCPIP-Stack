#include "headers.h"
#include "main.h"

#include <string.h>

#define SEQ_GT(a, b)  ((int32_t)((a) - (b)) > 0)
#define SEQ_GEQ(a, b) ((int32_t)((a) - (b)) >= 0)
tcpconnection tcptable[APP_TCP_CONNECTION_CAPACITY] APP_DTCM_BSS;
static uint32_t tcp_sequence_state;

static uint8_t newConnection(void);
static uint8_t getConnindx(TCP *tcp, uint8_t index);
static uint8_t TCP_Handshake( TCP_Handshakeinf HSinf);
static uint32_t TCP_NextSequence(void);
static void TCP_send_reset(uint8_t index, TCP *tcp, uint8_t tcp_hdr_len);
static void tcphandler_impl(uint8_t index);
static void TCP_send_reset_impl(uint8_t index, TCP *tcp, uint8_t tcp_hdr_len);

void TCP_Init(void)
{
    uint32_t seed = 0;
    if (HAL_RNG_GenerateRandomNumber(&hrng, &seed) != HAL_OK) {
        seed = DWT->CYCCNT ^ HAL_GetTick() ^ (uint32_t)(uintptr_t)&tcptable;
    }
    tcp_sequence_state = seed != 0 ? seed : 0xA341316CU;
}

static uint32_t TCP_NextSequence(void)
{
    uint32_t value = tcp_sequence_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    tcp_sequence_state = value != 0 ? value : 0xA341316CU;
    return tcp_sequence_state;
}

void TCP_close_connection(uint8_t index)
{
    if (index >= APP_TCP_CONNECTION_CAPACITY) {
        return;
    }

    for (int i = 0; i < ETH_TX_DESC_CNT; i++) {
        if (tcp_buff_lock[i].is_locked && tcp_buff_lock[i].conn_idx == index) {
            tcp_buff_lock[i].is_locked = 0;
            tcp_buff_lock[i].retry_count = 0;
        }
    }

    memset(&tcptable[index], 0, sizeof(tcptable[index]));
    tcptable[index].state = CLOSED;
    opf_connection_closed(index);
}

void tcphandler(uint8_t index)
{
    uint32_t dwt_start = DWT_START();
    tcphandler_impl(index);
    DWT_Record(DWT_METRIC_TCP_HANDLER, DWT_STOP(dwt_start));
}

static void tcphandler_impl(uint8_t index)
{
    if (inf[index].parsed_offset > inf[index].frame_length ||
        inf[index].frame_length - inf[index].parsed_offset < sizeof(TCP)) {
        return;
    }

    TCP *tcp = (TCP *)&RXbuffer[index][inf[index].parsed_offset];
    inf[index].tcp_pointer = tcp;

    uint8_t tcp_hdr_len = ((htons(tcp->doffResFlag) >> 12) & 0x0F) * 4;
    uint8_t ip_hdr_len = (inf[index].ip_pointer->Ver_IHL & 0x0F) * 4;
    uint16_t ip_total_len = htons(inf[index].ip_pointer->TotalLenght);
    if (ip_hdr_len < sizeof(IPv4header) || tcp_hdr_len < sizeof(TCP) ||
        ip_total_len < ip_hdr_len + tcp_hdr_len ||
        inf[index].parsed_offset + tcp_hdr_len > inf[index].frame_length) {
        return;
    }

    inf[index].parsed_offset += tcp_hdr_len;
    inf[index].payload_pointer = &RXbuffer[index][inf[index].parsed_offset];

    uint8_t Tindex = getConnindx(tcp, index);
    inf[index].tti = Tindex;

    if (Tindex == 255 && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_SYN) &&
        !GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_ACK) &&
        !GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_RST)) {
        if (htons(tcp->desPort) != APP_CONTROLLER_PORT) {
            TCP_send_reset(index, tcp, tcp_hdr_len);
            return;
        }

        uint8_t freeIdx = newConnection();
        if (freeIdx != 255) {
                uint32_t syn_start = DWT_START();
            tcptable[freeIdx].state = SYN_RECEIVED;
            tcptable[freeIdx].last_rx_time = tim6time;
            memcpy(tcptable[freeIdx].remote_ip.addr, inf[index].ip_pointer->source.addr, 4);
            tcptable[freeIdx].remote_port = htons(tcp->srcPort);
            tcptable[freeIdx].local_port = htons(tcp->desPort);
            tcptable[freeIdx].remote_seq_expected = htonl(tcp->seqNumber) + 1;
            tcptable[freeIdx].local_seq_sent = TCP_NextSequence() + 1U;
            tcptable[freeIdx].last_seen_ack = htonl(tcp->seqNumber);

            TCP_Handshakeinf handshake;
            handshake.data_len = ip_total_len - ip_hdr_len - tcp_hdr_len;
            handshake.flags = TCP_FLAG_ACK | TCP_FLAG_SYN;
            handshake.indx = freeIdx;
            uint8_t error = TCP_Handshake(handshake);
            if (error == 255) {
                tcptable[freeIdx].hskaeinf = handshake;
                tcptable[freeIdx].errstate = 255;
            }
                DWT_Record(DWT_METRIC_TCP_SYN_ACCEPT, DWT_STOP(syn_start));
        } else {
            TCP_send_reset(index, tcp, tcp_hdr_len);
        }
        return;
    }

    if (Tindex == 255) {
        if (!GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_RST)) {
            TCP_send_reset(index, tcp, tcp_hdr_len);
        }
        return;
    }

    if (GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_RST)) {
        TCP_close_connection(Tindex);
        return;
    }

    if(Tindex != 255)
    {
        tcptable[Tindex].last_rx_time = tim6time;
        uint32_t recived_seq = htonl(tcp->seqNumber);
        uint32_t recived_ack = htonl(tcp->ackNumber);

        uint16_t payload_len = ip_total_len - ip_hdr_len - tcp_hdr_len;
        uint8_t seq_increment = 0;
        if(GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_SYN) || GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_FIN))
        {
            seq_increment += 1;
        }

        uint8_t ack_valid = GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_ACK) &&
                            !SEQ_GT(recived_ack, tcptable[Tindex].local_seq_sent);
        if (ack_valid && SEQ_GEQ(recived_ack, tcptable[Tindex].local_seq_acked)) {
            tcptable[Tindex].last_seen_ack = tcptable[Tindex].local_seq_acked;
            tcptable[Tindex].local_seq_acked = recived_ack;
        }

        for(int i = 0; i < ETH_TX_DESC_CNT; i++)
        {
            if(ack_valid && tcp_buff_lock[i].conn_idx == Tindex && tcp_buff_lock[i].is_locked == 1)
            {
                if(SEQ_GT(recived_ack, tcp_buff_lock[i].seq_num))
                {
                    tcp_buff_lock[i].is_locked = 0;
                    tcp_buff_lock[i].retry_count = 0;
                }
            }
        }

        if(tcptable[Tindex].state == ESTABLISHED)
        {
            if(htons(tcp->desPort) == APP_CONTROLLER_PORT)
            {
                if ((payload_len != 0 || GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_FIN)) &&
                    recived_seq != tcptable[Tindex].remote_seq_expected) {
                    TCP_Handshakeinf duplicate_ack = {0};
                    duplicate_ack.flags = TCP_FLAG_ACK;
                    duplicate_ack.indx = Tindex;
                    TCP_Handshake(duplicate_ack);
                    return;
                }

                if (payload_len != 0) {
                    if (!opf_stream_receive(index, inf[index].payload_pointer, payload_len)) {
                        TCP_close_connection(Tindex);
                        return;
                    }

                    tcptable[Tindex].remote_seq_expected = recived_seq + payload_len;
                    TCP_Handshakeinf ack = {0};
                    ack.flags = TCP_FLAG_ACK;
                    ack.indx = Tindex;
                    TCP_Handshake(ack);
                }
            }
        }

        if(tcptable[Tindex].state == SYN_RECEIVED && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_ACK))
        {
            if (recived_ack == (tcptable[Tindex].local_seq_sent))
            {
                tcptable[Tindex].state = ESTABLISHED;
                for(int i = 0; i < ETH_TX_DESC_CNT; i++)
                {
                    if(tcp_buff_lock[i].conn_idx == Tindex && tcp_buff_lock[i].is_locked == 1)
                    {
                        tcp_buff_lock[i].is_locked = 0;
                        tcp_buff_lock[i].retry_count = 0;
                    }
                }
            } else {
                return;
            }
            if (payload_len != 0) {
                if (recived_seq != tcptable[Tindex].remote_seq_expected ||
                    !opf_stream_receive(index, inf[index].payload_pointer, payload_len)) {
                    TCP_close_connection(Tindex);
                    return;
                }
                tcptable[Tindex].remote_seq_expected = recived_seq + payload_len;
                TCP_Handshakeinf ack = {0};
                ack.flags = TCP_FLAG_ACK;
                ack.indx = Tindex;
                TCP_Handshake(ack);
            }
            if (payload_len == 0 && !GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_FIN)) {
                return;
            }
        }

        if(tcptable[Tindex].state == ESTABLISHED && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_FIN))
        {
            tcptable[Tindex].state = CLOSE_WAIT;
            tcptable[Tindex].local_seq_acked = recived_seq;
            tcptable[Tindex].remote_seq_expected = recived_seq + payload_len + seq_increment;

            TCP_Handshakeinf Hinf;
            Hinf.data_len = payload_len;
            Hinf.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
            Hinf.indx = Tindex;
            TCP_Handshake(Hinf);
            return;
        }

        if(tcptable[Tindex].state == CLOSE_WAIT &&
            GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_ACK) &&
            recived_ack == tcptable[Tindex].local_seq_sent)
        {
            TCP_close_connection(Tindex);
            return;
        }

        if(tcptable[Tindex].state == SYN_RECEIVED && GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_SYN))
        {
            TCP_Handshake(tcptable[Tindex].hskaeinf);
            return;
        }
    }
}

void TCP_clean_table()
{
    for (int connection = 0; connection < APP_TCP_CONNECTION_CAPACITY; connection++) {
        if (tcptable[connection].state != CLOSED) {
            if ((uint32_t)(tim6time - tcptable[connection].last_rx_time) >
                APP_TCP_INACTIVITY_TIMEOUT_TICKS) {
                TCP_close_connection(connection);
            }
        } else {
            for (int buffer = 0; buffer < ETH_TX_DESC_CNT; buffer++) {
                if (tcp_buff_lock[buffer].is_locked &&
                    tcp_buff_lock[buffer].conn_idx == connection) {
                    tcp_buff_lock[buffer].is_locked = 0;
                    tcp_buff_lock[buffer].retry_count = 0;
                }
            }
        }
    }
}

static uint8_t newConnection(void)
{
    for(int i = 0; i < APP_TCP_CONNECTION_CAPACITY; i++)
	{
		if(tcptable[i].state == CLOSED)
		{

			return i;
		}
	}

	return 255;
}

static uint8_t getConnindx(TCP *tcp, uint8_t index)
{
    for(int i = 0; i < APP_TCP_CONNECTION_CAPACITY; i++)
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
            if (tcp_buff_lock[i].conn_idx >= APP_TCP_CONNECTION_CAPACITY || tcptable[tcp_buff_lock[i].conn_idx].state == CLOSED) {
                tcp_buff_lock[i].is_locked = 0;
                tcp_buff_lock[i].retry_count = 0;
                continue;
            }

			if(SEQ_GEQ(tcp_buff_lock[i].seq_num, tcptable[tcp_buff_lock[i].conn_idx].local_seq_acked) && SEQ_GT(tim6time, tcp_buff_lock[i].sent_time))
			{
                if(tcp_buff_lock[i].retry_count < APP_TCP_MAX_RETRIES)
				{
                Transmit(tcp_buff_lock[i].data_len, i);
                if (tcp_buff_lock[i].is_locked) {
                    tcp_buff_lock[i].retry_count++;
                    tcp_buff_lock[i].sent_time = tim6time + 2;
                }
				} else
				{
                    TCP_close_connection(tcp_buff_lock[i].conn_idx);

				}

			}

		}

	}

    for(int i = 0; i < APP_TCP_CONNECTION_CAPACITY ; i++){

        if(tcptable[i].errstate == 255 && tcptable[i].state != CLOSED)
		{
            if (tcptable[i].D_ack_counter >= APP_TCP_MAX_RETRIES) {
                TCP_close_connection(i);
                continue;
            }

			uint8_t err =  TCP_Handshake(tcptable[i].hskaeinf);

			if(err == 255 )
			{
                tcptable[i].D_ack_counter++;
			}else if(err == 0)
			{
				tcptable[i].errstate = 0;
                tcptable[i].D_ack_counter = 0;

			}

		}

	}
}

static uint8_t TCP_Handshake( TCP_Handshakeinf HSinf)
{


    uint8_t buf_idx = GetTxBuff();

    if(buf_idx == 255)
    {

        return 255;
    }

    uint8_t *tx_ptr = TXbuffer[buf_idx];
    ETHFrame   *tx_eth = (ETHFrame *)tx_ptr;

    uint8_t succ = getmac(tcptable[HSinf.indx].remote_ip, &tx_eth->dest_add);
    if(succ == 0)
    {

        return 255;
    }

    tx_eth->ETHtype = htons(0x0800);
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
    

    uint8_t tcp_opt_len = 0;
    if(HSinf.flags == 0x12)
    {
        tcp_opt_len = 4;
    }


    IPv4header *tx_ip  = (IPv4header *)(tx_ptr + sizeof(ETHFrame));

    tx_ip->source = tempss.IPv4_add;
    tx_ip->destination = tcptable[HSinf.indx].remote_ip;
    tx_ip->Ver_IHL = 0x45;
    tx_ip->ToS = 0;
    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + tcp_opt_len);
    tx_ip->Identification = htons(100);
    tx_ip->Flag_Fregmantoff = htons(0x4000);
    tx_ip->TimeToLive = 64;
    tx_ip->Protocol = 6;
    
    tx_ip->HeaderChecksum = 0;

    TCP *tx_tcp = (TCP *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header));

    tx_tcp->desPort = htons(tcptable[HSinf.indx].remote_port);
    tx_tcp->srcPort = htons(tcptable[HSinf.indx].local_port);

    tx_tcp->checksum = 0;

    uint16_t doff = (sizeof(TCP) + tcp_opt_len) / 4;

    tx_tcp->doffResFlag = htons((doff << 12) | (000 << 9) | (HSinf.flags));

    if(HSinf.flags == 0X12)
    {
        uint8_t *tcp_opt_ptr = (uint8_t *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
        tcp_opt_ptr[0] = 0x02;
        tcp_opt_ptr[1] = 0x04;
        tcp_opt_ptr[2] = 0x05;
        tcp_opt_ptr[3] = 0xB4;

        tx_tcp->seqNumber = htonl(tcptable[HSinf.indx].local_seq_sent - 1U);
        tx_tcp->ackNumber = htonl(tcptable[HSinf.indx].remote_seq_expected);

    }
    else if (HSinf.flags == 0x11)
    {
        tx_tcp->seqNumber = htonl(tcptable[HSinf.indx].local_seq_sent);
        tcptable[HSinf.indx].local_seq_sent++;
        tx_tcp->ackNumber = htonl(tcptable[HSinf.indx].remote_seq_expected);

    }
    else if (HSinf.flags == TCP_FLAG_ACK)
    {
        tx_tcp->seqNumber = htonl(tcptable[HSinf.indx].local_seq_sent);
        tx_tcp->ackNumber = htonl(tcptable[HSinf.indx].remote_seq_expected);
    }

    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize = htons(HSinf.flags == TCP_FLAG_ACK ?
                               opf_stream_window(HSinf.indx) : APP_TCP_WINDOW_SIZE);

    if(HSinf.flags != TCP_FLAG_ACK)
    {
        tcp_buff_lock[buf_idx].is_locked = 1;
        tcp_buff_lock[buf_idx].seq_num =  tcptable[HSinf.indx].local_seq_sent;
        tcp_buff_lock[buf_idx].conn_idx = HSinf.indx;
    }
    else
    {
        tcp_buff_lock[buf_idx].is_locked = 0;
    }

    tcp_buff_lock[buf_idx].data_len = sizeof(IPv4header) + sizeof(TCP) + sizeof(ETHFrame) + tcp_opt_len;

    Transmit(tcp_buff_lock[buf_idx].data_len , buf_idx);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2;
    return 0;
}

static void TCP_send_reset(uint8_t index, TCP *tcp, uint8_t tcp_hdr_len)
{
    uint32_t dwt_start = DWT_START();
    TCP_send_reset_impl(index, tcp, tcp_hdr_len);
    DWT_Record(DWT_METRIC_TCP_RESET, DWT_STOP(dwt_start));
}

static void TCP_send_reset_impl(uint8_t index, TCP *tcp, uint8_t tcp_hdr_len)
{
    IPv4header *rx_ip = inf[index].ip_pointer;
    uint8_t ip_hdr_len = (rx_ip->Ver_IHL & 0x0F) * 4;
    uint16_t ip_total_len = htons(rx_ip->TotalLenght);
    if (ip_total_len < ip_hdr_len + tcp_hdr_len || tcp_hdr_len < sizeof(TCP)) {
        return;
    }

    uint8_t buffer = GetTxBuff();
    if (buffer >= ETH_TX_DESC_CNT) {
        return;
    }

    ETHFrame *rx_eth = inf[index].eth_pointer;
    ETHFrame *tx_eth = (ETHFrame *)TXbuffer[buffer];
    tx_eth->dest_add = rx_eth->src_add;
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
    tx_eth->ETHtype = htons(0x0800);

    IPv4header *tx_ip = (IPv4header *)(TXbuffer[buffer] + sizeof(ETHFrame));
    memset(tx_ip, 0, sizeof(*tx_ip));
    tx_ip->Ver_IHL = 0x45;
    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP));
    tx_ip->Identification = 0;
    tx_ip->Flag_Fregmantoff = htons(0x4000);
    tx_ip->TimeToLive = 64;
    tx_ip->Protocol = 6;
    tx_ip->source = rx_ip->destination;
    tx_ip->destination = rx_ip->source;

    TCP *tx_tcp = (TCP *)(TXbuffer[buffer] + sizeof(ETHFrame) + sizeof(IPv4header));
    memset(tx_tcp, 0, sizeof(*tx_tcp));
    tx_tcp->srcPort = tcp->desPort;
    tx_tcp->desPort = tcp->srcPort;
    tx_tcp->doffResFlag = htons((sizeof(TCP) / 4) << 12);
    tx_tcp->windowSize = 0;

    if (GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_ACK)) {
        tx_tcp->seqNumber = tcp->ackNumber;
        tx_tcp->doffResFlag |= htons(TCP_FLAG_RST);
    } else {
        uint32_t ack = htonl(tcp->seqNumber) + ip_total_len - ip_hdr_len - tcp_hdr_len;
        if (GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_SYN) ||
            GET_TCP_FLAG(tcp->doffResFlag, TCP_FLAG_FIN)) {
            ack++;
        }
        tx_tcp->ackNumber = htonl(ack);
        tx_tcp->doffResFlag |= htons(TCP_FLAG_RST | TCP_FLAG_ACK);
    }

    Transmit(sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP), buffer);
}
