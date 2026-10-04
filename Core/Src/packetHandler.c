#include "headers.h"
#include "main.h"

void packetHandler(uint8_t index)
{
    if (inf[index].frame_length < sizeof(ETHFrame) + sizeof(IPv4header)) {
        return;
    }

    IPv4header *packet = (IPv4header *)&RXbuffer[index][inf[index].parsed_offset];
    inf[index].ip_pointer = packet;
    uint8_t ip_header_length = (packet->Ver_IHL & 0x0FU) * 4U;
    uint16_t ip_total_length = htons(packet->TotalLenght);
    uint16_t available_length = inf[index].frame_length - sizeof(ETHFrame);
    if ((packet->Ver_IHL & 0xF0U) != 0x40U || ip_header_length < sizeof(IPv4header) ||
        ip_total_length < ip_header_length || ip_total_length > available_length ||
        (htons(packet->Flag_Fregmantoff) & 0x3FFFU) != 0) {
        return;
    }

    inf[index].parsed_offset += ip_header_length;


    if((packet->Ver_IHL & 0xF0U) == 0x40U)
    {
        switch(packet->Protocol)
        {
        case 1:

            icmpHandler(index);
            break;

        case 6:

            tcphandler(index);
            break;

        default:

            break;
        }
    }
    else
    {

    }
}
