#include "headers.h"
#include "main.h"

#define max_ports 64
opf_mac_table_entry ports[max_ports];

uint8_t sendhello(uint8_t index);
uint8_t OPFsendreply(uint8_t index, ofp_header *ofp);
uint8_t ProcessFT_reply(ofp_header *opf, uint8_t index);
uint8_t Sendmultireq(uint8_t index, ofp_header *ofp);
uint8_t process_multipart(uint8_t index, ofp_header *ofp);
uint8_t process_port_status(ofp_header *opf, uint8_t index);
uint8_t process_packet_in(uint8_t index, ofp_header *opf);
uint8_t send_packet_out(uint8_t index, uint32_t xid, uint32_t buffer_id, uint32_t in_port, uint32_t action_port, uint8_t *raw_frame, uint16_t frame_len);
uint8_t send_flow_mod(uint8_t index, uint8_t *dest_mac, uint32_t out_port);

fdb_entry_t mac_table[MAC_TABLE_SIZE];
switch_topology_t topology[MAX_SWITCHES];

typedef struct {
    ETHFrame eth;
    arpheader arp;
} ARPPacket;

static void GenArpreply(arpheader *arp, MAC_Addr_t target_mac, ipv4 tgip, ipv4 sender_ip)
{
    arp->Hardwer_Type = htons(1);
    arp->Protocol_Type = htons(0x0800);
    arp->Hardware_Length = 6;
    arp->Protocol_Length = 4;
    arp->Operation = htons(2);
    arp->sender_mac = *(MAC_Addr_t *)heth.Init.MACAddr;
    arp->sender_ip = sender_ip;
    arp->target_mac = target_mac;
    arp->target_ip = tgip;
}

uint32_t get_port_to_neighbor(uint8_t current_sw, uint8_t next_sw)
{
    for (int p = 0; p < 64; p++) {
        if (topology[current_sw].links[p].active == 1 &&
            topology[current_sw].links[p].neighbor_switch_idx == next_sw) {
            return p;
        }
    }
    return 0xFFFFFFFF;
}

uint8_t get_conn_idx_by_tti(uint8_t target_tti)
{
    for (int i = 0; i < 8; i++) {
        if (inf[i].tti == target_tti) {
            return i;
        }
    }
    return 255;
}

#define MAX_PATH_LEN 16

uint8_t find_shortest_path(uint8_t start_sw, uint8_t target_sw, uint8_t *path_out)
{
    if (start_sw == target_sw) {
        path_out[0] = start_sw;
        return 1;
    }

    uint8_t visited[MAX_SWITCHES] = {0};
    uint8_t parent[MAX_SWITCHES];
    for(int i = 0; i < MAX_SWITCHES; i++) parent[i] = 255;

    uint8_t queue[MAX_SWITCHES];
    uint8_t head = 0, tail = 0;

    queue[tail++] = start_sw;
    visited[start_sw] = 1;
    uint8_t found = 0;

    while (head < tail) {
        uint8_t curr = queue[head++];

        if (curr == target_sw) {
            found = 1;
            break;
        }

        for (int p = 0; p < 64; p++) {
            if (topology[curr].links[p].active == 1) {
                uint8_t neighbor = topology[curr].links[p].neighbor_switch_idx;
                if (!visited[neighbor]) {
                    visited[neighbor] = 1;
                    parent[neighbor] = curr;
                    queue[tail++] = neighbor;
                }
            }
        }
    }

    if (!found) return 0;

    uint8_t temp_path[MAX_PATH_LEN];
    uint8_t path_len = 0;
    uint8_t curr = target_sw;

    while (curr != 255 && path_len < MAX_PATH_LEN) {
        temp_path[path_len++] = curr;
        curr = parent[curr];
    }

    for (int i = 0; i < path_len; i++) {
        path_out[i] = temp_path[path_len - 1 - i];
    }

    return path_len;
}

void opf_handler(uint8_t index)
{
    ofp_header *opf = (ofp_header *)&RXbuffer[index][inf[index].parsed_offset];
    inf[index].parsed_offset += sizeof(ofp_header);

    uint8_t type = opf->type;
    uint16_t len = htons(opf->length);
    uint32_t xid = htonl(opf->xid);



    switch (type)
    {
    case 0:

        sendhello(index);
        break;
    case 2:

        OPFsendreply(index, opf);
        break;
    case 6:

        ProcessFT_reply(opf, index);
        Sendmultireq(index, opf);
        break;
    case 19:

        process_multipart(index, opf);
        break;
    case 12:

        process_port_status(opf, index);
        break;
    case 10:

        process_packet_in(index, opf);
        break;
    default:

        break;
    }
}

uint8_t process_lldp_packet(uint8_t index, ofp_header *opf, uint8_t *raw_ethernet_frame, uint32_t rx_port)
{
    uint8_t local_switch = inf[index].tti;
    uint32_t local_port = rx_port;

    uint8_t *tlv = raw_ethernet_frame + sizeof(ETHFrame);
    uint8_t remote_switch = 255;
    uint32_t remote_port = 0;

    uint16_t tlv_type = (htons(*(uint16_t*)tlv) >> 9) & 0x7F;
    uint16_t tlv_len = htons(*(uint16_t*)tlv) & 0x01FF;
    if (tlv_type == 1 && tlv[2] == 0x07) {
        remote_switch = htonl(*(uint32_t*)(tlv + 3));
    }
    tlv += 2 + tlv_len;

    tlv_type = (htons(*(uint16_t*)tlv) >> 9) & 0x7F;
    tlv_len = htons(*(uint16_t*)tlv) & 0x01FF;
    if (tlv_type == 2 && tlv[2] == 0x07) {
        remote_port = htonl(*(uint32_t*)(tlv + 3));
    }

    if (remote_switch != 255) {

        topology[local_switch].links[local_port].active = 1;
        topology[local_switch].links[local_port].neighbor_switch_idx = remote_switch;
        topology[local_switch].links[local_port].neighbor_port = remote_port;

        topology[remote_switch].links[remote_port].active = 1;
        topology[remote_switch].links[remote_port].neighbor_switch_idx = local_switch;
        topology[remote_switch].links[remote_port].neighbor_port = local_port;
    }
    return 0;
}

uint8_t send_lldp_packet_out(uint8_t index, uint32_t src_port)
{
    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) return 255;

    uint8_t *tx_ptr = TXbuffer[buf_idx];
    uint8_t tti = inf[index].tti; 

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    MAC_Addr_t switch_mac;
    if(getmac(tcptable[tti].remote_ip, &switch_mac) == 0) return 255;

    tx_eth->dest_add = switch_mac;
    tx_eth->src_add  = *(MAC_Addr_t *)heth.Init.MACAddr;
    tx_eth->ETHtype  = htons(0x0800);

    IPv4header *tx_ip = (IPv4header *)(tx_ptr + sizeof(ETHFrame));
    tx_ip->source           = tempss.IPv4_add;
    tx_ip->destination      = tcptable[tti].remote_ip;
    tx_ip->Ver_IHL          = 0x45;
    tx_ip->ToS              = 0;
    tx_ip->Identification   = htons(300);
    tx_ip->Flag_Fregmantoff = htons(0x4000); 
    tx_ip->TimeToLive       = 64;
    tx_ip->Protocol         = 6;
    tx_ip->HeaderChecksum   = 0;

    TCP *tx_tcp = (TCP *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header));
    tx_tcp->desPort       = htons(tcptable[tti].remote_port);
    tx_tcp->srcPort       = htons(tcptable[tti].local_port);
    uint16_t doff         = sizeof(TCP) / 4;
    tx_tcp->doffResFlag   = htons((doff << 12) | TCP_FLAG_ACK | TCP_FLAG_PSH);
    tx_tcp->seqNumber     = htonl(tcptable[tti].local_seq_sent);
    tx_tcp->ackNumber     = htonl(tcptable[tti].remote_seq_expected);
    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize    = htons(24000);
    tx_tcp->checksum      = 0;

    uint8_t *ofp_ptr           = tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP);
    ofp_header *ofh            = (ofp_header *)ofp_ptr;
    ofp_packet_out *out        = (ofp_packet_out *)(ofp_ptr + sizeof(ofp_header));
    ofp_action_output *ofp_ao  = (ofp_action_output *)((uint8_t *)out + sizeof(ofp_packet_out));
    uint8_t *inner_payload_ptr = (uint8_t *)ofp_ao + sizeof(ofp_action_output);

    ofh->version = 0x04;
    ofh->type    = 13;
    ofh->xid     = htonl(0xBB22);

    out->buffer_id   = htonl(0xFFFFFFFF);
    out->in_port     = htonl(0xFFFFFFFD);
    out->actions_len = htons(sizeof(ofp_action_output));
    memset(out->pad, 0, 6);

    ofp_ao->type    = htons(0);
    ofp_ao->len     = htons(sizeof(ofp_action_output));
    ofp_ao->port    = (src_port == 0) ? htonl(0xFFFFFFFC) : htonl(src_port);
    ofp_ao->max_len = htons(0xFFFF);
    memset(ofp_ao->pad, 0, 6);

    const uint16_t inner_frame_len = 64;
    memset(inner_payload_ptr, 0, inner_frame_len);

    ETHFrame *inner_eth = (ETHFrame *)inner_payload_ptr;
    const uint8_t lldp_mcast[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};
    memcpy(inner_eth->dest_add.addr, lldp_mcast, 6);
    inner_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
    inner_eth->ETHtype = htons(0x88CC);

    uint8_t *tlv = inner_payload_ptr + sizeof(ETHFrame);
    uint16_t chassis_hdr = htons((1 << 9) | 5);
    memcpy(tlv, &chassis_hdr, 2); tlv += 2;
    *tlv++ = 0x07;
    uint32_t sw_idx = htonl(tti);
    memcpy(tlv, &sw_idx, 4); tlv += 4;

    uint16_t port_hdr = htons((2 << 9) | 5);
    memcpy(tlv, &port_hdr, 2); tlv += 2;
    *tlv++ = 0x07;
    uint32_t port_net = htonl(src_port);
    memcpy(tlv, &port_net, 4); tlv += 4;

    uint16_t ttl_hdr = htons((3 << 9) | 2);
    memcpy(tlv, &ttl_hdr, 2); tlv += 2;
    uint16_t ttl_val = htons(120);
    memcpy(tlv, &ttl_val, 2); tlv += 2;

    uint16_t end_hdr = 0x0000;
    memcpy(tlv, &end_hdr, 2); tlv += 2;

    uint16_t ofp_total_len = sizeof(ofp_header) + sizeof(ofp_packet_out) + sizeof(ofp_action_output) + inner_frame_len;
    ofh->length = htons(ofp_total_len);

    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + ofp_total_len);
    tcptable[tti].local_seq_sent += ofp_total_len;

    tcp_buff_lock[buf_idx].is_locked   = 1;
    tcp_buff_lock[buf_idx].seq_num     = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx    = tti;
    tcp_buff_lock[buf_idx].data_len    = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + ofp_total_len;



    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    HAL_Delay(10);
    tcp_buff_lock[buf_idx].sent_time   = tim6time + 2;

    return 0;
}

uint8_t send_flow_mod(uint8_t tti, uint8_t *dest_mac, uint32_t out_port)
{
    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) return 255;

    uint8_t *tx_ptr = TXbuffer[buf_idx];

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) return 255; 

    tx_eth->ETHtype = htons(0x0800);
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;

    IPv4header *tx_ip = (IPv4header *)(tx_ptr + sizeof(ETHFrame));
    tx_ip->source = tempss.IPv4_add;
    tx_ip->destination = tcptable[tti].remote_ip;
    tx_ip->Ver_IHL = 0x45;
    tx_ip->ToS = 0;
    tx_ip->Identification = htons(200);
    tx_ip->Flag_Fregmantoff = htons(0x4000); 
    tx_ip->TimeToLive = 64;
    tx_ip->Protocol = 6; 
    tx_ip->HeaderChecksum = 0;

    TCP *tx_tcp = (TCP *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header));
    tx_tcp->desPort = htons(tcptable[tti].remote_port);
    tx_tcp->srcPort = htons(tcptable[tti].local_port);

    uint16_t doff = sizeof(TCP) / 4;
    tx_tcp->doffResFlag = htons((doff << 12) | (000 << 9) | TCP_FLAG_ACK | TCP_FLAG_PSH);
    tx_tcp->seqNumber = htonl(tcptable[tti].local_seq_sent);
    tx_tcp->ackNumber = htonl(tcptable[tti].remote_seq_expected);
    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize = htons(24000);
    tx_tcp->checksum = 0;

    uint8_t *ofp_ptr = tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP);
    
    ofp_header *ofh = (ofp_header *)ofp_ptr;
    ofp_flow_mod *fm = (ofp_flow_mod *)(ofp_ptr + 8);
    ofp_match_header *match = (ofp_match_header *)(ofp_ptr + 48);
    uint8_t *oxm = ofp_ptr + 52;
    ofp_instruction_actions *inst = (ofp_instruction_actions *)(ofp_ptr + 64);
    ofp_action_output *act_out = (ofp_action_output *)((uint8_t *)inst + 8);

    uint16_t ofp_total_len = 88;

    ofh->version = 0x04;
    ofh->type    = 14;
    ofh->length  = htons(ofp_total_len);
    ofh->xid     = htonl(0xAA11);

    memset(fm, 0, sizeof(ofp_flow_mod));
    fm->command      = 0;
    fm->table_id     = 0;
    fm->idle_timeout = htons(300);
    fm->hard_timeout = htons(0);
    fm->priority     = htons(100);
    fm->buffer_id    = 0xFFFFFFFF;
    fm->out_port     = 0xFFFFFFFF;
    fm->out_group    = 0xFFFFFFFF;

    match->type   = htons(1);
    match->length = htons(14);
    
    oxm[0] = 0x80; oxm[1] = 0x00;
    oxm[2] = 0x06;
    oxm[3] = 0x06;
    memcpy(&oxm[4], dest_mac, 6);
    oxm[10] = 0; oxm[11] = 0;

    inst->type = htons(4);
    inst->len  = htons(24);
    memset(inst->pad, 0, 4);

    memset(act_out, 0, sizeof(ofp_action_output));
    act_out->type    = htons(0);
    act_out->len     = htons(16);
    act_out->port    = htonl(out_port);
    act_out->max_len = htons(0xFFFF);

    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + ofp_total_len);
    tcptable[tti].local_seq_sent += ofp_total_len;

    tcp_buff_lock[buf_idx].is_locked = 1;
    tcp_buff_lock[buf_idx].seq_num = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx = tti;
    tcp_buff_lock[buf_idx].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + ofp_total_len;




    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    HAL_Delay(1);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2; 

    return 0;
}

uint8_t process_packet_in(uint8_t index, ofp_header *opf)
{
    ofp_packet_in_header *pin = (ofp_packet_in_header *)((uint8_t *)opf + sizeof(ofp_header));
    ofp_match_header *match = (ofp_match_header *)((uint8_t *)pin + sizeof(ofp_packet_in_header));
    oxm_in_port_t *in_port = (oxm_in_port_t *)((uint8_t *) match + sizeof(ofp_match_header));

    uint16_t match_len = htons(match->length);
    uint16_t match_padded_len = ((match_len + 7) / 8) * 8;

    uint8_t *raw_ethernet_frame = (uint8_t *)match + match_padded_len + 2;
    uint16_t frame_total_len = htons(pin->total_len);

    ETHFrame *eth = (ETHFrame *)raw_ethernet_frame;
    uint32_t rx_port = htonl(in_port->port_no);
    uint8_t current_switch_index = inf[index].tti;



    if (htons(eth->ETHtype) == 0x88CC) {

        process_lldp_packet(index, opf, raw_ethernet_frame, rx_port);
        return 0;
    }

    uint8_t is_isl = 0;
    if (rx_port < 64 && topology[current_switch_index].links[rx_port].active == 1) {
        is_isl = 1;
    }

    if (is_isl == 0) {
        uint8_t is_known_src = 0;
        for (int i = 0; i < MAC_TABLE_SIZE; i++) {
            if (mac_table[i].is_active == 1 &&
                memcmp(mac_table[i].mac, eth->src_add.addr, 6) == 0) {
                mac_table[i].port_no = rx_port;
                mac_table[i].tcp_index = current_switch_index;
                is_known_src = 1;
                break;
            }
        }

        if (is_known_src == 0) {
            for (int i = 0; i < MAC_TABLE_SIZE; i++) {
                if (mac_table[i].is_active == 0) {
                    memcpy(mac_table[i].mac, eth->src_add.addr, 6);
                    mac_table[i].port_no = rx_port;
                    mac_table[i].tcp_index = current_switch_index;
                    mac_table[i].is_active = 1;

                    break;
                }
            }
        }
    } else {

    }

    uint32_t action_port = 0xFFFFFFFB;
    const uint8_t mac_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    if (memcmp(eth->dest_add.addr, mac_bcast, 6) == 0 || (eth->dest_add.addr[0] & 0x01)) {

        action_port = 0xFFFFFFFB;
    }
    else {
        uint8_t target_switch_tti = 255;
        uint32_t target_host_port = 0;

        for (int i = 0; i < MAC_TABLE_SIZE; i++) {
            if (mac_table[i].is_active == 1 && memcmp(mac_table[i].mac, eth->dest_add.addr, 6) == 0) {
                target_switch_tti = mac_table[i].tcp_index;
                target_host_port = mac_table[i].port_no;
                break;
            }
        }

        if (target_switch_tti != 255) {

            uint8_t path[MAX_PATH_LEN];
            uint8_t path_len = find_shortest_path(current_switch_index, target_switch_tti, path);

            if (path_len > 0) {
                for (int p = 0; p < path_len; p++) {
                    uint8_t sw_tti = path[p];
                    uint32_t out_p;

                    if (path_len == 1 || p == path_len - 1) {
                        out_p = target_host_port;
                    } else {
                        out_p = get_port_to_neighbor(sw_tti, path[p+1]);
                    }

                    if (out_p != 0xFFFFFFFF) {
                        send_flow_mod(sw_tti, eth->dest_add.addr, out_p);
                    }
                }

                if (path_len == 1) {
                    action_port = target_host_port;
                } else {
                    action_port = get_port_to_neighbor(path[0], path[1]);
                }
            } else {

                action_port = 0xFFFFFFFB;
            }
        }
        else {

            action_port = 0xFFFFFFFB;
        }
    }

    send_packet_out(current_switch_index, opf->xid, 0xFFFFFFFF, in_port->port_no, htonl(action_port), raw_ethernet_frame, frame_total_len);

    return 0;
}

uint8_t send_packet_out(uint8_t tti, uint32_t xid, uint32_t buffer_id, uint32_t in_port, uint32_t action_port, uint8_t *raw_frame, uint16_t frame_len)
{
    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) return 255;

    uint8_t *tx_ptr = TXbuffer[buf_idx];

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) return 255;

    tx_eth->ETHtype = htons(0x0800);
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;

    IPv4header *tx_ip = (IPv4header *)(tx_ptr + sizeof(ETHFrame));
    tx_ip->source = tempss.IPv4_add;
    tx_ip->destination = tcptable[tti].remote_ip;
    tx_ip->Ver_IHL = 0x45;
    tx_ip->ToS = 0;
    tx_ip->Identification = htons(100);
    tx_ip->Flag_Fregmantoff = htons(0x4000);
    tx_ip->TimeToLive = 64;
    tx_ip->Protocol = 6;
    tx_ip->HeaderChecksum = 0;

    TCP *tx_tcp = (TCP *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header));
    tx_tcp->desPort = htons(tcptable[tti].remote_port);
    tx_tcp->srcPort = htons(tcptable[tti].local_port);

    uint16_t doff = sizeof(TCP) / 4;
    tx_tcp->doffResFlag = htons((doff << 12) | (000 << 9) | TCP_FLAG_ACK | TCP_FLAG_PSH);
    tx_tcp->seqNumber = htonl(tcptable[tti].local_seq_sent);
    tx_tcp->ackNumber = htonl(tcptable[tti].remote_seq_expected);
    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize = htons(24000);
    tx_tcp->checksum = 0;

    ofp_header *ofh = (ofp_header *) (tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
    ofp_packet_out *out = (ofp_packet_out *)((uint8_t *)ofh + sizeof(ofp_header));
    ofp_action_output *ofp_ao = (ofp_action_output *)((uint8_t *)out + sizeof(ofp_packet_out));
    uint8_t *tx_payload_ptr = (uint8_t *)ofp_ao + sizeof(ofp_action_output);

    ofh->version = 0x04;
    ofh->type    = 13;
    ofh->xid     = xid;

    out->buffer_id = buffer_id;
    out->in_port = in_port;
    out->actions_len = htons(sizeof(ofp_action_output));
    memset(out->pad, 0, 6);

    ofp_ao->type = htons(0);
    ofp_ao->len = htons(sizeof(ofp_action_output));
    ofp_ao->port = action_port;
    ofp_ao->max_len = htons(0xFFFF);
    memset(ofp_ao->pad, 0, 6);

    uint16_t payload_size = 0;
    if (buffer_id == 0xFFFFFFFF)
    {
        memcpy(tx_payload_ptr, raw_frame, frame_len);
        payload_size = frame_len;
    }

    uint16_t ofp_total_len = sizeof(ofp_header) + sizeof(ofp_packet_out) + sizeof(ofp_action_output) + payload_size;
    ofh->length = htons(ofp_total_len);

    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + ofp_total_len);
    tcptable[tti].local_seq_sent += ofp_total_len;

    tcp_buff_lock[buf_idx].is_locked = 1;
    tcp_buff_lock[buf_idx].seq_num = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx = tti;
    tcp_buff_lock[buf_idx].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + ofp_total_len;



    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2;

    return 0;
}

uint8_t process_port_status(ofp_header *opf, uint8_t index)
{
    ofp_port_status *status = (ofp_port_status *)((uint8_t *)opf + sizeof(ofp_header));
    uint32_t p_no = htonl(status->desc.port_no);
    uint8_t current_switch_tti = inf[index].tti;
    
    if ((status->reason == 0 || status->reason == 2) && p_no < 0xFFFFFF00)
    {

        send_lldp_packet_out(index, p_no);
    }
    return 0;
}

uint8_t process_multipart(uint8_t index, ofp_header *opf)
{
    uint16_t total_len = htons(opf->length);
    uint16_t port_count = (total_len - 16) / sizeof(ofp_port);
    ofp_port *port = (ofp_port *)((uint8_t *)opf + 16);
    uint8_t current_switch_tti = inf[index].tti;



    for (int i = 0; i < port_count; i++) 
    {
        for (int c = 0; c < max_ports; c++)
        {
            if(ports[c].is_free != 1)
            {
                ports[c].is_free = 1;
                memcpy(ports[c].mac, port->hw_addr, 6);
                ports[c].port_no = htonl(port->port_no);
                ports[c].index = current_switch_tti;

                if (ports[c].port_no < 0xFFFFFF00)
                {
                    send_lldp_packet_out(index, ports[c].port_no);
                }
                break;
            }
        }
        port++;
    }
    return 0;
}

uint8_t Sendmultireq(uint8_t index, ofp_header *ofp)
{
    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) return 255;
    uint8_t *tx_ptr = TXbuffer[buf_idx];
    uint8_t tti = inf[index].tti; 

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) return 255; 

    tx_eth->ETHtype = htons(0x0800);
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;

    IPv4header *tx_ip = (IPv4header *)(tx_ptr + sizeof(ETHFrame));
    tx_ip->source = tempss.IPv4_add;
    tx_ip->destination = tcptable[tti].remote_ip;
    tx_ip->Ver_IHL = 0x45;
    tx_ip->ToS = 0;
    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + sizeof(ofp_header) + sizeof(ofp_multipart_request));
    tx_ip->Identification = htons(100);
    tx_ip->Flag_Fregmantoff = htons(0x4000); 
    tx_ip->TimeToLive = 64;
    tx_ip->Protocol = 6; 
    tx_ip->HeaderChecksum = 0;

    TCP *tx_tcp = (TCP *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header));
    tx_tcp->desPort = htons(tcptable[tti].remote_port);
    tx_tcp->srcPort = htons(tcptable[tti].local_port);

    uint16_t doff = sizeof(TCP) / 4;
    tx_tcp->doffResFlag = htons((doff << 12) | (000 << 9) | TCP_FLAG_ACK | TCP_FLAG_PSH);
    tx_tcp->seqNumber = htonl(tcptable[tti].local_seq_sent);
    tx_tcp->ackNumber = htonl(tcptable[tti].remote_seq_expected);
    tx_tcp->checksum = 0;

    tcptable[tti].local_seq_sent += sizeof(ofp_header) + sizeof(ofp_multipart_request);

    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize = htons(24000);

    ofp_header *ofh = (ofp_header *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
    ofh->version = 0x04;
    ofh->type    = 18; 
    ofh->length  = htons(sizeof(ofp_header) + sizeof(ofp_multipart_request)); 
    ofh->xid     = ofp->xid;

    ofp_multipart_request *ofpmr = (ofp_multipart_request *)(ofh + 1); 
    ofpmr->type  = htons(13); 
    ofpmr->flags = 0;

    tcp_buff_lock[buf_idx].is_locked = 1;
    tcp_buff_lock[buf_idx].seq_num = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx = tti;
    tcp_buff_lock[buf_idx].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + sizeof(ofp_header) + sizeof(ofp_multipart_request);


    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    HAL_Delay(10);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2; 

    return 0;
}

uint8_t ProcessFT_reply(ofp_header *opf, uint8_t index)
{
    return 0;
}

uint8_t OPFsendreply(uint8_t index, ofp_header *ofp)
{
    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) return 255;
    uint8_t *tx_ptr = TXbuffer[buf_idx];
    uint8_t tti = inf[index].tti; 

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) return 255; 

    tx_eth->ETHtype = htons(0x0800);
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;

    IPv4header *tx_ip = (IPv4header *)(tx_ptr + sizeof(ETHFrame));
    tx_ip->source = tempss.IPv4_add;
    tx_ip->destination = tcptable[tti].remote_ip;
    tx_ip->Ver_IHL = 0x45;
    tx_ip->ToS = 0;
    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + sizeof(ofp_header)) ;
    tx_ip->Identification = htons(100);
    tx_ip->Flag_Fregmantoff = htons(0x4000); 
    tx_ip->TimeToLive = 64;
    tx_ip->Protocol = 6; 
    tx_ip->HeaderChecksum = 0;

    TCP *tx_tcp = (TCP *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header));
    tx_tcp->desPort = htons(tcptable[tti].remote_port);
    tx_tcp->srcPort = htons(tcptable[tti].local_port);

    uint16_t doff = sizeof(TCP) / 4;
    tx_tcp->doffResFlag = htons((doff << 12) | (000 << 9) | TCP_FLAG_ACK | TCP_FLAG_PSH);
    tx_tcp->seqNumber = htonl(tcptable[tti].local_seq_sent);
    tx_tcp->ackNumber = htonl(tcptable[tti].remote_seq_expected);
    tx_tcp->checksum = 0;

    tcptable[tti].local_seq_sent += sizeof(ofp_header);

    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize = htons(24000);

    ofp_header *ofh = (ofp_header *) (tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
    ofh->version = 0x04;
    ofh->type    = 3;
    ofh->length  = htons(8);
    ofh->xid     = ofp->xid;

    tcp_buff_lock[buf_idx].is_locked = 1;
    tcp_buff_lock[buf_idx].seq_num = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx = tti;
    tcp_buff_lock[buf_idx].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + sizeof(ofp_header);



    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    HAL_Delay(10);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2; 

    return 0;
}

uint8_t sendhello(uint8_t index)
{
    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) return 255;

    uint8_t *tx_ptr = TXbuffer[buf_idx];
    uint8_t tti = inf[index].tti; 

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) return 255; 

    tx_eth->ETHtype = htons(0x0800);
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;

    IPv4header *tx_ip = (IPv4header *)(tx_ptr + sizeof(ETHFrame));
    tx_ip->source = tempss.IPv4_add;
    tx_ip->destination = tcptable[tti].remote_ip;
    tx_ip->Ver_IHL = 0x45;
    tx_ip->ToS = 0;
    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + (2*sizeof(ofp_header)) + sizeof(ofp_hello_elem_versionbitmap));
    tx_ip->Identification = htons(100);
    tx_ip->Flag_Fregmantoff = htons(0x4000); 
    tx_ip->TimeToLive = 64;
    tx_ip->Protocol = 6; 
    tx_ip->HeaderChecksum = 0;

    TCP *tx_tcp = (TCP *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header));
    tx_tcp->desPort = htons(tcptable[tti].remote_port);
    tx_tcp->srcPort = htons(tcptable[tti].local_port);

    uint16_t doff = sizeof(TCP) / 4;
    tx_tcp->doffResFlag = htons((doff << 12) | (000 << 9) | TCP_FLAG_ACK | TCP_FLAG_PSH);
    tx_tcp->seqNumber = htonl(tcptable[tti].local_seq_sent);
    tx_tcp->ackNumber = htonl(tcptable[tti].remote_seq_expected);
    tx_tcp->checksum = 0;

    tcptable[tti].local_seq_sent += (2*sizeof(ofp_header)) + sizeof(ofp_hello_elem_versionbitmap);

    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize = htons(24000);

    ofp_header *ofh = (ofp_header *) (tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
    ofh->version = 0x04;
    ofh->type    = 0;
    ofh->length  = htons(16);    
    ofh->xid     = htonl(100);

    ofp_hello_elem_versionbitmap *elem = (ofp_hello_elem_versionbitmap *)((uint8_t *)ofh + sizeof(ofp_header));
    elem->type   = htons(1);
    elem->length = htons(8);
    elem->bitmap = htonl(0x00000010);

    ofp_header *ofpr = (ofp_header *)((uint8_t *)ofh + sizeof(ofp_header) + sizeof(ofp_hello_elem_versionbitmap));
    ofpr->version = 0x04;
    ofpr->type    = 5;
    ofpr->length  = htons(8);    
    ofpr->xid     = htonl(11);

    tcp_buff_lock[buf_idx].is_locked = 1;
    tcp_buff_lock[buf_idx].seq_num = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx = tti;
    tcp_buff_lock[buf_idx].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + (2 * sizeof(ofp_header)) + sizeof(ofp_hello_elem_versionbitmap);



    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    HAL_Delay(10);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2; 

    return 0;
}
