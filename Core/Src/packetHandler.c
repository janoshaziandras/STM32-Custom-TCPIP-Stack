#include "headers.h"
#include "main.h"




void packetHandler(uint8_t *RXframe, uint8_t index)
{
	IPv4header *packet = (IPv4header *) RXframe;

	if((packet->Ver_IHL & 0b11110000) == 0b01000000)
	{
		uint8_t ihl = packet->Ver_IHL & 0b00001111;


		switch(packet->Protocol)
		{
		case 1:

			icmpHandler(RXframe + ihl * 4, index);

		break;
		default:
			break;



		}


	}





}

