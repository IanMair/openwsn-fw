#include "gina_config.h"
#include "radio.h"
#include "packetfunctions.h"
#include "openqueue.h"
#include "stupidmac.h"

//===================================== variables =============================

uint8_t            radio_state;
uint8_t            default_radio_state;
uint8_t            default_channel;
OpenQueueEntry_t*  radioPacketReceived;
OpenQueueEntry_t*  radioPacketToSend;
#ifdef HYBRID_ARQ
#define RADIO_CORRUPTED_PACKET_BUFFER_LENGTH 1
uint8_t            temp_byte_corruption_index;
uint8_t            radio_corrupted_packet_counter;
uint8_t            radio_corrupted_packet_buffer[RADIO_CORRUPTED_PACKET_BUFFER_LENGTH][128];
#endif

//=========================== initialize the radio ============================

void radio_init() {
   radio_state = RADIO_STATE_STOPPED;
   // initialize radio-specific variables
   radioPacketReceived =  openqueue_getFreePacketBuffer();
   radioPacketReceived->creator = COMPONENT_RADIODRIVER;
   radioPacketReceived->owner   = COMPONENT_RADIODRIVER;

   // initialize communication between MSP430 and radio
   //-- 4-wire SPI
   spi_init();
   //-- RF_SLP_TR_CNTL (P4.7) pin (output)
   P4OUT  &= ~0x80;                              // set low
   P4DIR  |=  0x80;                              // configure as output
   //-- IRQ_RF (P1.6) pin (input)
   P1OUT  &= ~0x40;                              // set low
   P1DIR  &= ~0x40;                              // input direction
   P1IES  &= ~0x40;                              // interrup when transition is low-to-high
   P1IE   |=  0x40;                              // enable interrupt

   // configure radio
   spi_write_register(RG_TRX_STATE, CMD_FORCE_TRX_OFF);  // turn radio off
   spi_write_register(RG_IRQ_MASK, 0x08);                // fire interrupt only on TRX_END
   spi_read_register(RG_IRQ_STATUS);                     // deassert the interrupt pin (P1.6) in case high
   spi_write_register(RG_ANT_DIV, USE_CHIP_ANTENNA);     // always use chip antenna
#define RG_TRX_CTRL_1 0x04
#ifdef HYBRID_ARQ
   spi_write_register(RG_TRX_CTRL_1, 0x00);              // do not calculate CRC
   temp_byte_corruption_index = 0;
#else
   spi_write_register(RG_TRX_CTRL_1, 0x20);              // calculate CRC
#endif

   while((spi_read_register(RG_TRX_STATUS) & 0x1F) != TRX_OFF); //busy wait until radio status is TRX_OFF
   radio_state = RADIO_STATE_STARTED;
   default_radio_state = radio_state;

   radioPacketToSend = NULL;
#ifdef HYBRID_ARQ
   radio_corrupted_packet_counter=0;
#endif
}

void radio_sleep() {
   while((spi_read_register(RG_TRX_STATUS) & 0x1F) != TRX_OFF); //busy wait until radio status is TRX_OFF
   P4DIR   |=   0x80; //set P4.7 as output
   P4OUT   &=  ~0x80; //set P4.7 low
   P4OUT   |=   0x80; //set P4.7 high to enable sleep mode
   //P4OUT   &=  ~0x80; //set P4.7 low again
}

//=========================== sending a packet ================================

error_t radio_send(OpenQueueEntry_t* packet) {
   if (radioPacketToSend!=NULL) {
      return E_FAIL;
   }
   radioPacketToSend = packet;
   //radioPacketToSend->owner = COMPONENT_RADIODRIVER; don't do for resend
   //set channel
   radio_state = RADIO_STATE_SETTING_CHANNEL;
   if (packet->l1_channel < 11 || packet->l1_channel > 26){
      packet->l1_channel = 26;
   }
   spi_write_register(RG_PHY_CC_CCA,0x20+packet->l1_channel);
   //add 1B length
   packetfunctions_reserveHeaderSize(packet,1);
   packet->payload[0] = packet->length-1;   // length (not counting length field)
#ifdef HYBRID_ARQ
   //calculate CRC
   packetfunctions_calculateCRC(packet);
   temp_byte_corruption_index = (temp_byte_corruption_index+1)%5;
   if (temp_byte_corruption_index==1 || temp_byte_corruption_index==3){
      *(packet->payload+temp_byte_corruption_index)=0xff;
   }
#endif
   //add 1B SPI address
   packetfunctions_reserveHeaderSize(packet,1);
   packet->payload[0] = 0x60;
   //load packet in TXFIFO
   radio_state = RADIO_STATE_LOADING_PACKET;
   spi_write_buffer(radioPacketToSend);
   radio_state = RADIO_STATE_READY_TX;
   //turn on radio's PLL
   radio_state = RADIO_STATE_TRANSMITTING;
   spi_write_register(RG_TRX_STATE, CMD_PLL_ON);
   while((spi_read_register(RG_TRX_STATUS) & 0x1F) != PLL_ON); //busy wait until radio status is PLL_ON
   //send packet
   P4OUT |=  0x80;
   P4OUT &= ~0x80;
   return E_SUCCESS;
}

//=========================== receiving a packet ==============================

void radio_rxOn(uint8_t channel) {
   //set channel
   radio_state = RADIO_STATE_SETTING_CHANNEL;
   if (channel < 11 || channel > 26){
      channel = 26;
   }
   spi_write_register(RG_PHY_CC_CCA,0x20+channel);
   default_channel = channel;
   //put radio in reception mode
   spi_write_register(RG_TRX_STATE, CMD_RX_ON);
   while((spi_read_register(RG_TRX_STATUS) & 0x1F) != RX_ON); //busy wait until radio status is PLL_ON
   radio_state = RADIO_STATE_READY_RX;
   default_radio_state = radio_state;
}

void isr_radio() {
#ifdef HYBRID_ARQ
   uint8_t i,j,k,distance;
   uint8_t diff_positions[2];  // indicates the location of the two flipped bytes
   uint8_t options[2][2];      // [position][options]
#endif
   uint8_t temp_reg_value;
   OpenQueueEntry_t* radioPacketReceived_new;
   spi_read_register(RG_IRQ_STATUS);          // reading IRQ_STATUS causes IRQ_RF (P1.6) to go low
   switch (radio_state) {
      case RADIO_STATE_READY_RX:
         radio_state = RADIO_STATE_RECEIVING;
         // initialize the reception buffer
         radioPacketReceived->payload = &(radioPacketReceived->packet[0]);
         // check if CRC is correct
         temp_reg_value = spi_read_register(RG_PHY_RSSI);
         radioPacketReceived->l1_rssi =  temp_reg_value & 0x1F;      // last 5 lsb's are RSSI
         radioPacketReceived->l1_crc  = (temp_reg_value & 0x80)>>7;  // msb is whether packet passed CRC
         // copy packet from buffer
         spi_read_buffer(radioPacketReceived,2);  // first read only 2 bytes to receive the length
         radioPacketReceived->length = radioPacketReceived->payload[1];
         if (radioPacketReceived->length<=127) {
            spi_read_buffer(radioPacketReceived,1+1+radioPacketReceived->length+1); // then retrieve all packet (including 1B SPI address, 1B length, 1B LQI)
            // shift by 2B "header" (answer received when MSP sent SPI address, 1B length). Length does not change.
            radioPacketReceived->payload += 2;
            // read 1B "footer" (LQI) and store that information
            radioPacketReceived->l1_lqi = radioPacketReceived->payload[radioPacketReceived->length];
#ifdef HYBRID_ARQ
            if (radioPacketReceived->l1_crc==0) {
               //compare received packet with each packet in radio_corrupted_packet_buffer
               //until find one with exactly 2 differences
               i=0;
               j=0;
               while (i<RADIO_CORRUPTED_PACKET_BUFFER_LENGTH) {
                  distance = 0;
                  if (radio_corrupted_packet_buffer[i][0]==radioPacketReceived->length){
                     for (j=0;j<radioPacketReceived->length;j++) {
                        if (radio_corrupted_packet_buffer[i][j+1]!=*(radioPacketReceived->payload+j)){
                           diff_positions[distance]=j;
                           options[distance][0]=radio_corrupted_packet_buffer[i][j+1];
                           options[distance][1]=*(radioPacketReceived->payload+j);
                           distance++;
                        }
                     }
                     if (distance==2){
                        break;
                     }
                  }
                  i++;
               }
               if (i<RADIO_CORRUPTED_PACKET_BUFFER_LENGTH) {
                  //for all four combinations, create a new packet, check CRC
                  for (j=0;j<2;j++) {
                     *(radioPacketReceived->payload+diff_positions[0])=options[0][j];
                     for (k=0;k<2;k++) {
                        *(radioPacketReceived->payload+diff_positions[1])=options[1][k];
                        if (packetfunctions_checkCRC(radioPacketReceived)==TRUE) {
                           radioPacketReceived->l1_crc=1;
                        }
                     }
                  }
               }
               if (radioPacketReceived->l1_crc==0) {
                  //store corrupted packet in radio_corrupted_packet_buffer
                  radio_corrupted_packet_buffer[radio_corrupted_packet_counter][0] = radioPacketReceived->length;
                  memcpy(&radio_corrupted_packet_buffer[radio_corrupted_packet_counter][1],
                        radioPacketReceived->payload,
                        radioPacketReceived->length);
                  radio_corrupted_packet_counter = (radio_corrupted_packet_counter+1)%RADIO_CORRUPTED_PACKET_BUFFER_LENGTH;
               }
            }
#endif
            if (radioPacketReceived->l1_crc==1) {
               // get a new space for receiving packet
               radioPacketReceived_new =  openqueue_getFreePacketBuffer();
               if (radioPacketReceived_new!=NULL) {
                  radioPacketReceived_new->creator = COMPONENT_RADIODRIVER;
                  radioPacketReceived_new->owner   = COMPONENT_RADIODRIVER;
                  // pass packet to higher layer
                  radio_state=RADIO_STATE_READY_RX;
                  radio_packet_received(radioPacketReceived);
                  radioPacketReceived = radioPacketReceived_new;
               } else {
                  openqueue_reset_entry(radioPacketReceived);
                  radioPacketReceived->creator = COMPONENT_RADIODRIVER;
                  radioPacketReceived->owner   = COMPONENT_RADIODRIVER;
                  radio_state=RADIO_STATE_READY_RX;
               }
            } else {
               radio_state=RADIO_STATE_READY_RX;
            }
         } else {
            radio_state=RADIO_STATE_READY_RX;
         }
         break;
      case RADIO_STATE_TRANSMITTING:
         //remove 1B SPI destination address
         packetfunctions_tossHeader(radioPacketToSend,1);
         //remove 1B length field
         packetfunctions_tossHeader(radioPacketToSend,1);
         //signal sendDone to upper layer
         stupidmac_sendDone(radioPacketToSend,E_SUCCESS);
         radioPacketToSend = NULL;
         //return to default state
         if (default_radio_state==RADIO_STATE_READY_RX) {
            radio_rxOn(default_channel);
         } else {
            radio_state=RADIO_STATE_STARTED;
         }
         break;
   }
}

//=========================== turning radio off ===============================

void radio_rfOff() {
   spi_write_register(RG_TRX_STATE, CMD_FORCE_TRX_OFF);  // turn radio off
   while((spi_read_register(RG_TRX_STATUS) & 0x1F) != TRX_OFF); //busy wait until radio status is TRX_OFF
   radio_state = RADIO_STATE_STARTED;
   default_radio_state = radio_state;
}
