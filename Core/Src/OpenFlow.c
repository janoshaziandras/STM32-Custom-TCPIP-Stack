#include "headers.h"
#include "main.h"

#include <string.h>

opf_mac_table_entry ports[APP_SWITCH_CAPACITY][APP_SWITCH_PORT_CAPACITY] APP_AXI_SRAM;
static uint32_t mac_table_last_seen[MAC_TABLE_SIZE] APP_AXI_SRAM;
static uint32_t topology_last_seen[MAX_SWITCHES][APP_SWITCH_PORT_CAPACITY] APP_AXI_SRAM;

uint8_t sendhello(uint8_t index, ofp_header *request);
uint8_t send_features_reply(uint8_t index, ofp_header *request);
uint8_t OPFsendreply(uint8_t index, ofp_header *ofp, uint16_t message_length);
uint8_t Sendmultireq(uint8_t index, ofp_header *ofp);
uint8_t process_multipart(uint8_t index, ofp_header *ofp);
uint8_t process_port_status(ofp_header *opf, uint8_t index);
uint8_t process_packet_in(uint8_t index, ofp_header *opf, uint16_t message_length);
uint8_t send_packet_out(uint8_t index, uint32_t xid, uint32_t buffer_id, uint32_t in_port, uint32_t action_port, uint8_t *raw_frame, uint16_t frame_len);
uint8_t send_flow_mod(uint8_t index, uint8_t *dest_mac, uint32_t out_port);
static void opf_dispatch_message(uint8_t index, uint8_t *message, uint16_t message_length);
static uint8_t openflow_rx_buffer[APP_TCP_CONNECTION_CAPACITY][APP_OPENFLOW_RX_BUFFER_SIZE] APP_AXI_SRAM;
static uint16_t openflow_rx_length[APP_TCP_CONNECTION_CAPACITY] APP_AXI_SRAM;

fdb_entry_t mac_table[MAC_TABLE_SIZE] APP_AXI_SRAM;
switch_topology_t topology[MAX_SWITCHES] APP_AXI_SRAM;

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
    if (current_sw >= APP_SWITCH_CAPACITY || next_sw >= APP_SWITCH_CAPACITY) {
        return 0xFFFFFFFF;
    }
    for (int p = 0; p < APP_SWITCH_PORT_CAPACITY; p++) {
        if (topology[current_sw].links[p].active == 1 &&
            topology[current_sw].links[p].neighbor_switch_idx == next_sw) {
            return p;
        }
    }
    return 0xFFFFFFFF;
}

uint8_t find_shortest_path(uint8_t start_sw, uint8_t target_sw, uint8_t *path_out)
{
    if (start_sw >= APP_SWITCH_CAPACITY || target_sw >= APP_SWITCH_CAPACITY || path_out == NULL) {
        return 0;
    }
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

        for (int p = 0; p < APP_SWITCH_PORT_CAPACITY; p++) {
            if (topology[curr].links[p].active == 1) {
                uint8_t neighbor = topology[curr].links[p].neighbor_switch_idx;
                if (neighbor >= APP_SWITCH_CAPACITY) {
                    continue;
                }
                if (!visited[neighbor]) {
                    visited[neighbor] = 1;
                    parent[neighbor] = curr;
                    queue[tail++] = neighbor;
                }
            }
        }
    }

    if (!found) return 0;

    uint8_t temp_path[APP_MAX_PATH_LENGTH];
    uint8_t path_len = 0;
    uint8_t curr = target_sw;

    while (curr != 255 && path_len < APP_MAX_PATH_LENGTH) {
        temp_path[path_len++] = curr;
        curr = parent[curr];
    }

    for (int i = 0; i < path_len; i++) {
        path_out[i] = temp_path[path_len - 1 - i];
    }

    return path_len;
}

uint8_t opf_stream_receive(uint8_t index, const uint8_t *data, uint16_t length)
{
    if (index >= ETH_RX_DESC_CNT || data == NULL) {
        return 0;
    }
    uint8_t connection = inf[index].tti;
    if (connection >= APP_TCP_CONNECTION_CAPACITY) {
        return 0;
    }
    if (openflow_rx_length[connection] > APP_OPENFLOW_RX_BUFFER_SIZE ||
        length > APP_OPENFLOW_RX_BUFFER_SIZE - openflow_rx_length[connection]) {
        DWT_PacketDropped();
        return 0;
    }

    uint8_t *stream = openflow_rx_buffer[connection];
    uint16_t *stream_length = &openflow_rx_length[connection];
    memcpy(&stream[*stream_length], data, length);
    *stream_length += length;

    while (*stream_length >= sizeof(ofp_header)) {
        uint32_t parse_start = DWT_START();
        ofp_header *header = (ofp_header *)stream;
        uint16_t message_length = htons(header->length);
        if (header->version != 0x04U || message_length < sizeof(ofp_header) ||
            message_length > APP_OPENFLOW_RX_BUFFER_SIZE) {
            *stream_length = 0;
            return 0;
        }
        DWT_Record(DWT_METRIC_OPENFLOW_PARSE, DWT_STOP(parse_start));
        if (*stream_length < message_length) {
            break;
        }

        uint32_t dwt_start = DWT_START();
        opf_dispatch_message(index, stream, message_length);
        DWT_Record(DWT_METRIC_OPENFLOW_HANDLER, DWT_STOP(dwt_start));

        *stream_length -= message_length;
        if (*stream_length != 0) {
            memmove(stream, &stream[message_length], *stream_length);
        }
    }
    return 1;
}

uint16_t opf_stream_window(uint8_t connection)
{
    if (connection >= APP_TCP_CONNECTION_CAPACITY ||
        openflow_rx_length[connection] >= APP_OPENFLOW_RX_BUFFER_SIZE) {
        return 0;
    }
    return APP_OPENFLOW_RX_BUFFER_SIZE - openflow_rx_length[connection];
}

static void opf_dispatch_message(uint8_t index, uint8_t *message, uint16_t message_length)
{
    ofp_header *opf = (ofp_header *)message;
    switch (opf->type)
    {
    case 0:
        if (sendhello(index, opf) == 0) {
            Sendmultireq(index, opf);
        }
        break;
    case 2:
        OPFsendreply(index, opf, message_length);
        break;
    case 5:
            if (message_length < sizeof(ofp_header)) {
                break;
            }
            if (send_features_reply(index, opf) == 0) {
                Sendmultireq(index, opf);
            }
            break;
    case 6:
        break;
    case 19:

        process_multipart(index, opf);
        break;
    case 12:
        if (message_length >= sizeof(ofp_header) + sizeof(ofp_port_status)) {
            process_port_status(opf, index);
        }
        break;
    case 10:
        process_packet_in(index, opf, message_length);
        break;
    default:

        break;
    }
}

uint8_t process_lldp_packet(uint8_t index, ofp_header *opf, uint8_t *raw_ethernet_frame,
                            uint32_t rx_port, uint16_t frame_length)
{
    (void)opf;
    uint8_t local_switch = inf[index].tti;
    uint32_t remote_switch = UINT32_MAX;
    uint32_t remote_port = UINT32_MAX;

    if (local_switch >= APP_SWITCH_CAPACITY || rx_port >= APP_SWITCH_PORT_CAPACITY ||
        frame_length < sizeof(ETHFrame) + 2U) {
        return 255;
    }

    uint8_t *cursor = raw_ethernet_frame + sizeof(ETHFrame);
    uint16_t remaining = frame_length - sizeof(ETHFrame);
    while (remaining >= 2U) {
        uint16_t raw_header;
        memcpy(&raw_header, cursor, sizeof(raw_header));
        uint16_t header = htons(raw_header);
        uint16_t tlv_type = header >> 9;
        uint16_t tlv_length = header & 0x01FFU;
        if (tlv_length > remaining - 2U) {
            return 255;
        }
        if (tlv_type == 0U) {
            break;
        }

        if ((tlv_type == 1U || tlv_type == 2U) && tlv_length == 5U && cursor[2] == 0x07U) {
            uint32_t identifier;
            memcpy(&identifier, cursor + 3U, sizeof(identifier));
            if (tlv_type == 1U) {
                remote_switch = htonl(identifier);
            } else {
                remote_port = htonl(identifier);
            }
        }

        cursor += 2U + tlv_length;
        remaining -= 2U + tlv_length;
    }

    if (remote_switch >= APP_SWITCH_CAPACITY || remote_port >= APP_SWITCH_PORT_CAPACITY) {
        return 255;
    }

    topology[local_switch].links[rx_port].active = 1;
    topology[local_switch].links[rx_port].neighbor_switch_idx = remote_switch;
    topology[local_switch].links[rx_port].neighbor_port = remote_port;
    topology_last_seen[local_switch][rx_port] = tim6time;
    topology[remote_switch].links[remote_port].active = 1;
    topology[remote_switch].links[remote_port].neighbor_switch_idx = local_switch;
    topology[remote_switch].links[remote_port].neighbor_port = rx_port;
    topology_last_seen[remote_switch][remote_port] = tim6time;
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
    if(getmac(tcptable[tti].remote_ip, &switch_mac) == 0) {
        ReleaseTxBuff(buf_idx);
        return 255;
    }

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
    tx_tcp->windowSize    = htons(APP_TCP_WINDOW_SIZE);
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
    tcp_buff_lock[buf_idx].sent_time   = tim6time + 2;

    return 0;
}

uint8_t send_flow_mod(uint8_t tti, uint8_t *dest_mac, uint32_t out_port)
{
    if (tti >= APP_TCP_CONNECTION_CAPACITY || dest_mac == NULL) {
        return 255;
    }
    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) {
        DWT_PacketDropped();
        return 255;
    }

    uint8_t *tx_ptr = TXbuffer[buf_idx];

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) { ReleaseTxBuff(buf_idx); return 255; }

    uint32_t message_start = DWT_START();
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
    tx_tcp->windowSize = htons(APP_TCP_WINDOW_SIZE);
    tx_tcp->checksum = 0;

    uint8_t *ofp_ptr = tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP);
    
    ofp_header *ofh = (ofp_header *)ofp_ptr;
    ofp_flow_mod *fm = (ofp_flow_mod *)(ofp_ptr + sizeof(ofp_header));
    ofp_match_header *match = (ofp_match_header *)(ofp_ptr + sizeof(ofp_header) + sizeof(ofp_flow_mod));
    uint8_t *oxm = (uint8_t *)(match + 1);
    uint16_t match_len = sizeof(ofp_match_header) + 4U + sizeof(MAC_Addr_t);
    uint16_t padded_match_len = (match_len + 7U) & ~7U;
    uint8_t *instruction_ptr = (uint8_t *)match + padded_match_len;
    ofp_instruction_actions *inst = (ofp_instruction_actions *)instruction_ptr;
    ofp_action_output *act_out = (ofp_action_output *)((uint8_t *)inst + sizeof(ofp_instruction_actions));

    uint16_t ofp_total_len = sizeof(ofp_header) + sizeof(ofp_flow_mod) + padded_match_len +
                             sizeof(ofp_instruction_actions) + sizeof(ofp_action_output);

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

    memset(match, 0, padded_match_len);
    match->type   = htons(1);
    match->length = htons(match_len);
    
    oxm[0] = 0x80; oxm[1] = 0x00;
    oxm[2] = 0x06;
    oxm[3] = 0x06;
    memcpy(&oxm[4], dest_mac, 6);

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




    DWT_Record(DWT_METRIC_MESSAGE_GENERATION, DWT_STOP(message_start));
    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2; 

    return 0;
}

uint8_t process_packet_in(uint8_t index, ofp_header *opf, uint16_t message_length)
{
    if (message_length < sizeof(ofp_header) + sizeof(ofp_packet_in_header) + sizeof(ofp_match_header)) {
        return 255;
    }
    uint32_t extraction_start = DWT_START();

    ofp_packet_in_header *pin = (ofp_packet_in_header *)((uint8_t *)opf + sizeof(ofp_header));
    ofp_match_header *match = (ofp_match_header *)((uint8_t *)pin + sizeof(ofp_packet_in_header));

    uint32_t match_len = htons(match->length);
    uint32_t match_offset = (uint32_t)((uint8_t *)match - (uint8_t *)opf);
    if (match->type != htons(1U) ||
        match_len < sizeof(ofp_match_header) + sizeof(oxm_in_port_t) ||
        match_len > message_length - match_offset) {
        return 255;
    }
    uint32_t match_padded_len = (match_len + 7U) & ~7U;
    uint32_t in_port_offset = match_offset + sizeof(ofp_match_header);
    if (in_port_offset + sizeof(oxm_in_port_t) > match_offset + match_len ||
        match_offset + match_padded_len + 2U > message_length) {
        return 255;
    }
    oxm_in_port_t *in_port = (oxm_in_port_t *)((uint8_t *)match + sizeof(ofp_match_header));
    uint32_t in_port_oxm = htonl(in_port->oxm_header);
    if ((in_port_oxm >> 16) != 0x8000U ||
        ((in_port_oxm >> 9) & 0x7FU) != 0U ||
        (in_port_oxm & 0x100U) != 0U ||
        (in_port_oxm & 0xFFU) != sizeof(uint32_t)) {
        return 255;
    }

    uint8_t *raw_ethernet_frame = (uint8_t *)match + match_padded_len + 2;
    uint16_t frame_total_len = htons(pin->total_len);
    uint32_t frame_offset = (uint32_t)(raw_ethernet_frame - (uint8_t *)opf);
    if (frame_offset > message_length || frame_total_len < sizeof(ETHFrame) ||
        message_length - frame_offset < sizeof(ETHFrame) || inf[index].tti >= APP_SWITCH_CAPACITY ||
        (pin->buffer_id == OFP_NO_BUFFER && frame_total_len > message_length - frame_offset)) {
        return 255;
    }

    ETHFrame *eth = (ETHFrame *)raw_ethernet_frame;
    uint32_t rx_port = htonl(in_port->port_no);
    uint8_t current_switch_index = inf[index].tti;
    if (rx_port >= APP_SWITCH_PORT_CAPACITY) {
        return 255;
    }

    DWT_Record(DWT_METRIC_PACKET_IN_EXTRACTION, DWT_STOP(extraction_start));

    if (htons(eth->ETHtype) == 0x88CC) {

        process_lldp_packet(index, opf, raw_ethernet_frame, rx_port, frame_total_len);
        return 0;
    }

    uint8_t is_isl = 0;
    if (rx_port < APP_SWITCH_PORT_CAPACITY && topology[current_switch_index].links[rx_port].active == 1) {
        is_isl = 1;
    }

    if (is_isl == 0) {
        uint8_t is_known_src = 0;
        for (int i = 0; i < MAC_TABLE_SIZE; i++) {
            if (mac_table[i].is_active == 1 &&
                memcmp(mac_table[i].mac, eth->src_add.addr, 6) == 0) {
                mac_table[i].port_no = rx_port;
                mac_table[i].tcp_index = current_switch_index;
                mac_table_last_seen[i] = tim6time;
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
                    mac_table_last_seen[i] = tim6time;

                    break;
                }
            }
        }
    } else {

    }

    uint32_t action_port = 0xFFFFFFFB;
    const uint8_t mac_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t path[APP_MAX_PATH_LENGTH];
    uint8_t path_len = 0;
    uint8_t target_switch_tti = 255;
    uint32_t target_host_port = 0;
    uint32_t routing_start = DWT_START();

    if (memcmp(eth->dest_add.addr, mac_bcast, 6) == 0 || (eth->dest_add.addr[0] & 0x01)) {
        action_port = 0xFFFFFFFB;
    }
    else {
        for (int i = 0; i < MAC_TABLE_SIZE; i++) {
            if (mac_table[i].is_active == 1 && memcmp(mac_table[i].mac, eth->dest_add.addr, 6) == 0) {
                target_switch_tti = mac_table[i].tcp_index;
                target_host_port = mac_table[i].port_no;
                break;
            }
        }

        if (target_switch_tti != 255) {
            path_len = find_shortest_path(current_switch_index, target_switch_tti, path);
            if (path_len > 0) {
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

    DWT_Record(DWT_METRIC_ROUTING, DWT_STOP(routing_start));

    if (path_len > 0) {
        for (int p = 0; p < path_len; p++) {
            uint8_t sw_tti = path[p];
            uint32_t out_p = (path_len == 1 || p == path_len - 1)
                ? target_host_port
                : get_port_to_neighbor(sw_tti, path[p + 1]);
            if (out_p != 0xFFFFFFFF) {
                send_flow_mod(sw_tti, eth->dest_add.addr, out_p);
            }
        }
    }

    send_packet_out(current_switch_index, opf->xid, pin->buffer_id, in_port->port_no,
                    htonl(action_port), raw_ethernet_frame, frame_total_len);

    return 0;
}

void mac_table_aging(void)
{
    uint32_t dwt_start = DWT_START();
    for (int i = 0; i < MAC_TABLE_SIZE; i++) {
        if (mac_table[i].is_active &&
            (tim6time - mac_table_last_seen[i]) > APP_MAC_AGING_TIMEOUT_TICKS) {
            memset(&mac_table[i], 0, sizeof(mac_table[i]));
            mac_table_last_seen[i] = 0;
        }
    }
    DWT_Record(DWT_METRIC_FDB_AGING, DWT_STOP(dwt_start));
}

void topology_aging(void)
{
    for (uint8_t sw = 0; sw < APP_SWITCH_CAPACITY; sw++) {
        for (uint16_t port = 0; port < APP_SWITCH_PORT_CAPACITY; port++) {
            link_t link = topology[sw].links[port];
            if (!link.active || (tim6time - topology_last_seen[sw][port]) <= APP_LLDP_AGING_TIMEOUT_TICKS) {
                continue;
            }

            if (link.neighbor_switch_idx < APP_SWITCH_CAPACITY &&
                link.neighbor_port < APP_SWITCH_PORT_CAPACITY) {
                link_t *reverse = &topology[link.neighbor_switch_idx].links[link.neighbor_port];
                if (reverse->active && reverse->neighbor_switch_idx == sw &&
                    reverse->neighbor_port == port) {
                    memset(reverse, 0, sizeof(*reverse));
                    topology_last_seen[link.neighbor_switch_idx][link.neighbor_port] = 0;
                }
            }
            memset(&topology[sw].links[port], 0, sizeof(topology[sw].links[port]));
            topology_last_seen[sw][port] = 0;
        }
    }
}

void opf_connection_closed(uint8_t index)
{
    if (index < APP_TCP_CONNECTION_CAPACITY) {
        openflow_rx_length[index] = 0;
    }
    if (index >= MAX_SWITCHES) {
        return;
    }

    for (int port = 0; port < APP_SWITCH_PORT_CAPACITY; port++) {
        link_t link = topology[index].links[port];
        if (link.active && link.neighbor_switch_idx < MAX_SWITCHES && link.neighbor_port < APP_SWITCH_PORT_CAPACITY) {
            link_t *reverse = &topology[link.neighbor_switch_idx].links[link.neighbor_port];
            if (reverse->active && reverse->neighbor_switch_idx == index && reverse->neighbor_port == (uint32_t)port) {
                memset(reverse, 0, sizeof(*reverse));
                topology_last_seen[link.neighbor_switch_idx][link.neighbor_port] = 0;
            }
        }
    }

    memset(&topology[index], 0, sizeof(topology[index]));
    memset(topology_last_seen[index], 0, sizeof(topology_last_seen[index]));

    for (int i = 0; i < MAC_TABLE_SIZE; i++) {
        if (mac_table[i].is_active && mac_table[i].tcp_index == index) {
            memset(&mac_table[i], 0, sizeof(mac_table[i]));
            mac_table_last_seen[i] = 0;
        }
    }

    for (int i = 0; i < APP_SWITCH_PORT_CAPACITY; i++) {
        memset(&ports[index][i], 0, sizeof(ports[index][i]));
    }
}

uint8_t send_packet_out(uint8_t tti, uint32_t xid, uint32_t buffer_id, uint32_t in_port, uint32_t action_port, uint8_t *raw_frame, uint16_t frame_len)
{
    const uint32_t fixed_length = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) +
                                  sizeof(ofp_header) + sizeof(ofp_packet_out) +
                                  sizeof(ofp_action_output);
        if (tti >= APP_TCP_CONNECTION_CAPACITY ||
                    (buffer_id == 0xFFFFFFFF &&
                     (raw_frame == NULL || frame_len > APP_PACKET_OUT_MAX_FRAME_SIZE ||
                      frame_len > APP_ETH_TX_BUFFER_SIZE - fixed_length))) {
        return 255;
    }

    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) {
        DWT_PacketDropped();
        return 255;
    }

    uint8_t *tx_ptr = TXbuffer[buf_idx];

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) { ReleaseTxBuff(buf_idx); return 255; }

    uint32_t message_start = DWT_START();
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
    tx_tcp->windowSize = htons(APP_TCP_WINDOW_SIZE);
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



    DWT_Record(DWT_METRIC_MESSAGE_GENERATION, DWT_STOP(message_start));
    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2;

    return 0;
}

uint8_t process_port_status(ofp_header *opf, uint8_t index)
{
    ofp_port_status *status = (ofp_port_status *)((uint8_t *)opf + sizeof(ofp_header));
    uint32_t p_no = htonl(status->desc.port_no);
    uint8_t current_switch_tti = inf[index].tti;

    if (current_switch_tti >= APP_SWITCH_CAPACITY || p_no >= APP_SWITCH_PORT_CAPACITY) {
        return 0;
    }

    if (status->reason == 1) {
        link_t link = topology[current_switch_tti].links[p_no];
        if (link.active && link.neighbor_switch_idx < APP_SWITCH_CAPACITY &&
            link.neighbor_port < APP_SWITCH_PORT_CAPACITY) {
            memset(&topology[link.neighbor_switch_idx].links[link.neighbor_port], 0,
                   sizeof(topology[link.neighbor_switch_idx].links[link.neighbor_port]));
            topology_last_seen[link.neighbor_switch_idx][link.neighbor_port] = 0;
        }
        memset(&topology[current_switch_tti].links[p_no], 0,
               sizeof(topology[current_switch_tti].links[p_no]));
        topology_last_seen[current_switch_tti][p_no] = 0;
        for (int port_slot = 0; port_slot < APP_SWITCH_PORT_CAPACITY; port_slot++) {
            if (ports[current_switch_tti][port_slot].is_free &&
                ports[current_switch_tti][port_slot].port_no == p_no) {
                memset(&ports[current_switch_tti][port_slot], 0,
                       sizeof(ports[current_switch_tti][port_slot]));
            }
        }
        for (int i = 0; i < MAC_TABLE_SIZE; i++) {
            if (mac_table[i].is_active && mac_table[i].tcp_index == current_switch_tti &&
                mac_table[i].port_no == p_no) {
                memset(&mac_table[i], 0, sizeof(mac_table[i]));
                mac_table_last_seen[i] = 0;
            }
        }
        return 0;
    }

    if ((status->reason == 0 || status->reason == 2) && p_no < 0xFFFFFF00) {
        send_lldp_packet_out(index, p_no);
    }
    return 0;
}

uint8_t process_multipart(uint8_t index, ofp_header *opf)
{
    uint16_t total_len = htons(opf->length);
    uint8_t current_switch_tti = inf[index].tti;
    if (current_switch_tti >= APP_SWITCH_CAPACITY || total_len < sizeof(ofp_header) + sizeof(ofp_multipart_request)) {
        return 255;
    }

    ofp_multipart_request *multipart = (ofp_multipart_request *)((uint8_t *)opf + sizeof(ofp_header));
    if (htons(multipart->type) != 13U) {
        return 0;
    }

    uint16_t body_len = total_len - sizeof(ofp_header) - sizeof(ofp_multipart_request);
    if (body_len < sizeof(ofp_port)) {
        return 0;
    }

    uint16_t port_count = body_len / sizeof(ofp_port);
    if (port_count > APP_SWITCH_PORT_CAPACITY) {
        port_count = APP_SWITCH_PORT_CAPACITY;
    }

    ofp_port *port = (ofp_port *)((uint8_t *)multipart + sizeof(ofp_multipart_request));
    for (int i = 0; i < port_count; i++)
    {
        uint32_t port_number = htonl(port->port_no);
        int slot = -1;
        for (int c = 0; c < APP_SWITCH_PORT_CAPACITY; c++)
        {
            if (ports[current_switch_tti][c].is_free &&
                ports[current_switch_tti][c].port_no == port_number) {
                slot = c;
                break;
            }
            if (!ports[current_switch_tti][c].is_free && slot < 0) {
                slot = c;
            }
        }
        if (slot >= 0) {
            opf_mac_table_entry *entry = &ports[current_switch_tti][slot];
            entry->is_free = 1;
            memcpy(entry->mac, port->hw_addr, 6);
            entry->port_no = port_number;
            entry->index = current_switch_tti;

            if (entry->port_no < 0xFFFFFF00U) {
                send_lldp_packet_out(index, entry->port_no);
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
    if(succ == 0) { ReleaseTxBuff(buf_idx); return 255; }

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
    tx_tcp->windowSize = htons(APP_TCP_WINDOW_SIZE);

    ofp_header *ofh = (ofp_header *)(tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
    ofh->version = 0x04;
    ofh->type    = 18; 
    ofh->length  = htons(sizeof(ofp_header) + sizeof(ofp_multipart_request)); 
    ofh->xid     = ofp->xid;

    ofp_multipart_request *ofpmr = (ofp_multipart_request *)(ofh + 1); 
    memset(ofpmr, 0, sizeof(*ofpmr));
    ofpmr->type  = htons(13); 
    ofpmr->flags = 0;

    tcp_buff_lock[buf_idx].is_locked = 1;
    tcp_buff_lock[buf_idx].seq_num = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx = tti;
    tcp_buff_lock[buf_idx].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + sizeof(ofp_header) + sizeof(ofp_multipart_request);


    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2; 

    return 0;
}

uint8_t OPFsendreply(uint8_t index, ofp_header *ofp, uint16_t message_length)
{
    uint32_t fixed_length = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP);
    if (message_length < sizeof(ofp_header) || message_length > APP_TCP_MSS ||
        message_length > APP_ETH_TX_BUFFER_SIZE - fixed_length) {
        return 255;
    }

    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) return 255;
    uint8_t *tx_ptr = TXbuffer[buf_idx];
    uint8_t tti = inf[index].tti; 

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) { ReleaseTxBuff(buf_idx); return 255; }

    tx_eth->ETHtype = htons(0x0800);
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;

    IPv4header *tx_ip = (IPv4header *)(tx_ptr + sizeof(ETHFrame));
    tx_ip->source = tempss.IPv4_add;
    tx_ip->destination = tcptable[tti].remote_ip;
    tx_ip->Ver_IHL = 0x45;
    tx_ip->ToS = 0;
    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + message_length);
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

    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize = htons(APP_TCP_WINDOW_SIZE);

    ofp_header *ofh = (ofp_header *) (tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
    ofh->version = 0x04;
    ofh->type    = 3;
    ofh->length  = htons(message_length);
    ofh->xid     = ofp->xid;

    if (message_length > sizeof(ofp_header)) {
        memcpy(ofh + 1, ofp + 1, message_length - sizeof(ofp_header));
    }

    tcptable[tti].local_seq_sent += message_length;

    tcp_buff_lock[buf_idx].is_locked = 1;
    tcp_buff_lock[buf_idx].seq_num = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx = tti;
    tcp_buff_lock[buf_idx].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + message_length;



    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2; 

    return 0;
}

uint8_t sendhello(uint8_t index, ofp_header *request)
{
    uint8_t buf_idx = GetTxBuff();
    if(buf_idx == 255) return 255;

    uint8_t *tx_ptr = TXbuffer[buf_idx];
    uint8_t tti = inf[index].tti; 

    ETHFrame *tx_eth = (ETHFrame *)tx_ptr;
    uint8_t succ = getmac(tcptable[tti].remote_ip, &tx_eth->dest_add);
    if(succ == 0) { ReleaseTxBuff(buf_idx); return 255; }

    tx_eth->ETHtype = htons(0x0800);
    tx_eth->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;

    IPv4header *tx_ip = (IPv4header *)(tx_ptr + sizeof(ETHFrame));
    tx_ip->source = tempss.IPv4_add;
    tx_ip->destination = tcptable[tti].remote_ip;
    tx_ip->Ver_IHL = 0x45;
    tx_ip->ToS = 0;
    tx_ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + sizeof(ofp_header) + sizeof(ofp_hello_elem_versionbitmap));
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

    tcptable[tti].local_seq_sent += sizeof(ofp_header) + sizeof(ofp_hello_elem_versionbitmap);

    tx_tcp->urgentPointer = 0;
    tx_tcp->windowSize = htons(APP_TCP_WINDOW_SIZE);

    ofp_header *ofh = (ofp_header *) (tx_ptr + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
    ofh->version = 0x04;
    ofh->type    = 0;
    ofh->length  = htons(16);    
    ofh->xid     = request->xid;

    ofp_hello_elem_versionbitmap *elem = (ofp_hello_elem_versionbitmap *)((uint8_t *)ofh + sizeof(ofp_header));
    elem->type   = htons(1);
    elem->length = htons(8);
    elem->bitmap = htonl(0x00000010);

    tcp_buff_lock[buf_idx].is_locked = 1;
    tcp_buff_lock[buf_idx].seq_num = htonl(tx_tcp->seqNumber);
    tcp_buff_lock[buf_idx].conn_idx = tti;
    tcp_buff_lock[buf_idx].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP) + sizeof(ofp_header) + sizeof(ofp_hello_elem_versionbitmap);



    Transmit(tcp_buff_lock[buf_idx].data_len, buf_idx);
    tcp_buff_lock[buf_idx].sent_time = tim6time + 2; 

    return 0;
}

uint8_t send_features_reply(uint8_t index, ofp_header *request)
{
    uint8_t buffer = GetTxBuff();
    if (buffer == 255) {
        return 255;
    }

    uint8_t connection = inf[index].tti;
    uint8_t *tx = TXbuffer[buffer];
    ETHFrame *ethernet = (ETHFrame *)tx;
    if (getmac(tcptable[connection].remote_ip, &ethernet->dest_add) == 0) {
        ReleaseTxBuff(buffer);
        return 255;
    }

    ethernet->src_add = *(MAC_Addr_t *)heth.Init.MACAddr;
    ethernet->ETHtype = htons(0x0800);

    IPv4header *ip = (IPv4header *)(tx + sizeof(ETHFrame));
    ip->source = tempss.IPv4_add;
    ip->destination = tcptable[connection].remote_ip;
    ip->Ver_IHL = 0x45;
    ip->ToS = 0;
    ip->TotalLenght = htons(sizeof(IPv4header) + sizeof(TCP) + sizeof(ofp_header) + sizeof(ofp_switch_features));
    ip->Identification = 0;
    ip->Flag_Fregmantoff = htons(0x4000);
    ip->TimeToLive = 64;
    ip->Protocol = 6;
    ip->HeaderChecksum = 0;

    TCP *tcp = (TCP *)(tx + sizeof(ETHFrame) + sizeof(IPv4header));
    tcp->desPort = htons(tcptable[connection].remote_port);
    tcp->srcPort = htons(tcptable[connection].local_port);
    tcp->doffResFlag = htons((sizeof(TCP) / 4U << 12) | TCP_FLAG_ACK | TCP_FLAG_PSH);
    tcp->seqNumber = htonl(tcptable[connection].local_seq_sent);
    tcp->ackNumber = htonl(tcptable[connection].remote_seq_expected);
    tcp->urgentPointer = 0;
    tcp->windowSize = htons(APP_TCP_WINDOW_SIZE);
    tcp->checksum = 0;

    ofp_header *reply = (ofp_header *)(tx + sizeof(ETHFrame) + sizeof(IPv4header) + sizeof(TCP));
    reply->version = 0x04;
    reply->type = 6;
    reply->length = htons(sizeof(ofp_header) + sizeof(ofp_switch_features));
    reply->xid = request->xid;

    ofp_switch_features *features = (ofp_switch_features *)(reply + 1);
    memset(features, 0, sizeof(*features));
    features->datapath_id = ((uint64_t)heth.Init.MACAddr[0] << 40) |
                            ((uint64_t)heth.Init.MACAddr[1] << 32) |
                            ((uint64_t)heth.Init.MACAddr[2] << 24) |
                            ((uint64_t)heth.Init.MACAddr[3] << 16) |
                            ((uint64_t)heth.Init.MACAddr[4] << 8) |
                            heth.Init.MACAddr[5];
    uint64_t datapath_id = features->datapath_id;
    features->datapath_id = ((uint64_t)htonl((uint32_t)datapath_id) << 32) |
                            htonl((uint32_t)(datapath_id >> 32));
    features->n_buffers = htonl(ETH_TX_DESC_CNT);
    features->n_tables = 1;

    uint16_t openflow_length = sizeof(ofp_header) + sizeof(ofp_switch_features);
    uint16_t tcp_length = sizeof(TCP) + openflow_length;
    ip->TotalLenght = htons(sizeof(IPv4header) + tcp_length);
    tcptable[connection].local_seq_sent += openflow_length;

    tcp_buff_lock[buffer].is_locked = 1;
    tcp_buff_lock[buffer].seq_num = tcptable[connection].local_seq_sent - openflow_length;
    tcp_buff_lock[buffer].conn_idx = connection;
    tcp_buff_lock[buffer].data_len = sizeof(ETHFrame) + sizeof(IPv4header) + tcp_length;
    Transmit(tcp_buff_lock[buffer].data_len, buffer);
    tcp_buff_lock[buffer].sent_time = tim6time + 2;
    return 0;
}
