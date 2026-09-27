#include "headers.h"
#include "main.h"

void packetHandler(uint8_t index)
{
    IPv4header *packet = (IPv4header *)&RXbuffer[index][inf[index].parsed_offset];
    inf[index].ip_pointer = packet;
    inf[index].parsed_offset += (packet->Ver_IHL & 0b00001111) * 4;

    uint8_t *src = packet->source.addr;
    uint8_t *dst = packet->destination.addr;


    if((packet->Ver_IHL & 0b11110000) == 0b01000000)
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
