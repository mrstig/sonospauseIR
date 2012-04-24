/*
 * ======================================================================
 * SonosIR - Arduino sketch for infrared control of Sonos ZonePlayer (c) 2010
 * Simon Long, KuDaTa Software
 * 
 * Original Sonos control Arduino sketch by Jan-Piet Mens. Uses the Arduino
 * IRremote library by Ken Shirriff.
 * 
 * Use is at the user's own risk - no warranty is expressed or implied.
 * ======================================================================
 *
 * Changes:
 *	2010-04-24 simon added volume control
 */

#include <SPI.h>
#include <Ethernet.h>
#include <IRremote.h>
#include <PString.h>
#include <SPI_VFD.h>
#include <Flash.h>

/*----------------------------------------------------------------------*/
/* Macros and constants */
/*----------------------------------------------------------------------*/

/* Sonos SOAP command packet skeleton */
FLASH_STRING(sonos_cmdh, "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>");
FLASH_STRING(sonos_cmdp, " xmlns:u=\"urn:schemas-upnp-org:service:");
FLASH_STRING(sonos_cmdq, ":1\"><InstanceID>0</InstanceID>");
FLASH_STRING(sonos_cmdf, "</s:Body></s:Envelope>");

// see FSM.java
const char* fsm = "\x26\x3c\x0\x61\x18\x0\x6d\x9\x0\x70\x0\x0\x3b\x0\x26\x70\x0\x0\x6f\x0\x0\x73\x0\x0\x3b\x0\x27\x67\x9\x0\x74\x0\x0\x3b\x0\x3e\x6c\x9\x0\x74\x0\x0\x3b\x0\x3c\x71\x0\x0\x75\x0\x0\x6f\x0\x0\x74\x0\x0\x3b\x0\x22\xc3\x0\x0\x84\x3\x41\x85\x3\x41\x86\x3\x41\x96\x3\x4f\x98\x3\x4f\xa4\x3\x61\xa5\x3\x61\xa6\x3\x61\xb6\x3\x6f\xb8\x0\x6f";


/* Sonos SOAP command packet enumeration */
#define SONOS_PAUSE  0
#define SONOS_PLAY   1
#define SONOS_PREV   2
#define SONOS_NEXT   3
#define SONOS_SEEK   4
#define SONOS_NORMAL 5
#define SONOS_REPEAT 6
#define SONOS_SHUFF  7
#define SONOS_SHUREP 8
#define SONOS_MODE   9
#define SONOS_POSIT  10
#define SONOS_GETVOL 11
#define SONOS_SETVOL 12
#define SONOS_TRACK  13

/* State machine for A-B repeat and intro scan functions */
#define MODE_NORMAL 0
#define MODE_SCAN   1
#define MODE_A      2
#define MODE_AB     3

/* Arduino pin to which IR receiver is connected */
#define IR_PIN 8

/* IRremote hex codes for received remote commands */
//#define REMOTE_PLAY      0x35
//#define REMOTE_PAUSE     0x30
//#define REMOTE_PREV      0x21
//#define REMOTE_NEXT      0x20
#define REMOTE_SHUFFLE   0x1C
#define REMOTE_REPEAT    0x1D
#define REMOTE_AB        0x3B
#define REMOTE_SCAN      0x2B
#define REMOTE_REV       0x32
#define REMOTE_FWD       0x34
//#define REMOTE_VOLU      0x10
//#define REMOTE_VOLD      0x11

//Thomson universal set to Philips code = 0200
// 0C 0D
// 20 10
// 21 11

#define REMOTE_PLAY     0xDD // mini
#define REMOTE_PAUSE    0x5D // mini pwr
#define REMOTE_NEXT     0x3D // mini
#define REMOTE_VOLU     0x10
#define REMOTE_PREV     0x21
#define REMOTE_VOLD     0x11
#define REMOTE_MODE     0x9D // mini




/* Enable DEBUG for serial debug output */
//
//#define DEBUG

/*----------------------------------------------------------------------*/
/* Global variables */
/*----------------------------------------------------------------------*/

/* IP address of ZonePlayer to control */
IPAddress stue(192, 168,2,192);

/* Millisecond timer values */
unsigned long   lastcmd = 0;
unsigned long   lastrew = 0;
unsigned long   lasttrackpoll =  0;

#define MAX_DATA 40
/* Buffers used for Sonos data reception */
char            data1[MAX_DATA];
char            data2[MAX_DATA];

/* Global null buffer used to disable data reception */
char            nullbuf[1] = {0};

/* IR receiver control */
IRrecv          irrecv(IR_PIN);
decode_results  results;

/* Ethernet control */
EthernetClient          client;

/* display */
SPI_VFD vfd(2, 3, 4);

/*----------------------------------------------------------------------*/
/* Function defintions */
/*----------------------------------------------------------------------*/


uint8_t * heapptr, * stackptr;
void check_mem() {
#ifdef DEBUG 
stackptr = (uint8_t *)malloc(4);          // use stackptr temporarily
  heapptr = stackptr;                     // save value of heap pointer
  free(stackptr);      // free up the memory again (sets stackptr to 0)
  stackptr =  (uint8_t *)(SP);           // save value of stack pointer
  Serial.print("m: ");
  Serial.println(stackptr - heapptr);
#endif
}

/*----------------------------------------------------------------------*/
/* setup - configures Arduino */

void 
setup()
{
	uint8_t         mac[6] = {0xBE, 0xEF, 0xEE, 0x00, 0x20, 0x09};

	delay(3000);

	/* initialise Ethernet connection */
	Ethernet.begin(mac);

	/* initialise IR receiver */
	irrecv.enableIRIn();
      


        vfd.begin(20, 2);
        for (byte thisByte = 0; thisByte < 4; thisByte++) {
          // print the value of each byte of the IP address:
          vfd.print(Ethernet.localIP()[thisByte], DEC);
          if ( thisByte < 3 ) vfd.print("."); 
        }        
     //vfd.autoscroll();

#ifdef DEBUG
	Serial.begin(9600);
#endif
}

/*----------------------------------------------------------------------*/
/* loop - called repeatedly by Arduino */

void 
loop()
{
	//int             res;

  if ( millis() > lasttrackpoll + 1000) {
//      vfd.setCursor(0, 1);
//      vfd.print("Fetching track      ");      
     sonos(SONOS_TRACK, data1, data2);
     vfd.clear();
      vfd.setCursor(0, 0);
      vfd.print(data1);
      vfd.setCursor(0, 1);
      vfd.print(data2);
#ifdef DEBUG
 Serial.println(data1);
 Serial.println(data2);
#endif
      lasttrackpoll = millis();
  }

	/* look to see if a packet has been received from the IR receiver */
	if (irrecv.decode(&results)) {
#ifdef DEBUG
		Serial.println(results.value, HEX);
check_mem();
Serial.flush();
#endif

		/*
		 * only process if this is the first packet for 200ms -
		 * prevents multiple operations
		 */
		if (millis() > (lastcmd + 200)) {
			/* compare received IR against known commands */
			switch (results.value & 0xFF) {

			case REMOTE_PLAY:
                                vfd.setCursor(0, 1);
                                vfd.print("Play          ");
				sonos(SONOS_PLAY, nullbuf, nullbuf);
				break;

			case REMOTE_PAUSE:
                                vfd.setCursor(0, 1);
                                vfd.print("Pause         ");
				sonos(SONOS_PAUSE, nullbuf, nullbuf);
				break;

			case REMOTE_NEXT:
                                vfd.setCursor(0, 1);
                                vfd.print("Next          ");
				sonos(SONOS_NEXT, nullbuf, nullbuf);
				break;

			case REMOTE_PREV:
                  		sonos(SONOS_PREV, nullbuf, nullbuf);
				break;

			case REMOTE_MODE:
                  		sonos(SONOS_POSIT, data1, nullbuf);
                                vfd.setCursor(0, 1);
                                vfd.print("Posit: ");
                                vfd.print(data1);
				break;

			/* store time at which last IR command was processed */
			lastcmd = millis();
                        }
		}
		/* get ready to receive next IR command */
		irrecv.resume();
	}

}

/*----------------------------------------------------------------------*/
/* seconds - converts supplied string in format hh:mm:ss to seconds */

int 
seconds(char *str)
{
	int             hrs, mins, secs;
	sscanf(str, "%d:%d:%d", &hrs, &mins, &secs);
	hrs *= 60;
	hrs += mins;
	hrs *= 60;
	hrs += secs;
	return hrs;
}



/*----------------------------------------------------------------------*/
/* out - outputs supplied string to Ethernet client */

void 
out(const char *s)
{
//  while(client.free() == 0 )
//    delay(5);
	client.print(s);
//        client.print("\n");
#ifdef DEBUG
	Serial.print(s);
//Serial.flush();
#endif
}



/*----------------------------------------------------------------------*/
/* sonos - sends a command packet to the ZonePlayer */

void 
sonos(int cmd, char *resp1, char *resp2)
{
	//char          buf[512];
	char            buf[120];
	char            cmdbuf[32];
	char            extra[64];
	char            service[20];
	char           *ptr1;
	char           *ptr2;
	char           *optr;
	char            copying;
	unsigned long   timeout;

	extra[0] = 0;
	PString pservice(service, sizeof(service));
        pservice.print("AVTransport");
        PString pextra(extra, sizeof(extra)) ;
        PString pcmdbuf(cmdbuf, sizeof(cmdbuf)) ;
        PString pbuf(buf, sizeof(buf));
        


		/*
		 * prepare the data strings to go into the desired command
		 * packet
		 */
		switch (cmd) {
		case SONOS_PLAY:
			pcmdbuf.print("Play");
			pextra.print("<Speed>1</Speed>");
			break;

		case SONOS_PAUSE:
			pcmdbuf.print("Pause");
			break;

		case SONOS_PREV:
			pcmdbuf.print("Previous");
			break;

		case SONOS_NEXT:
			pcmdbuf.print("Next");
			break;


		case SONOS_MODE:
			pcmdbuf.print("GetTransportSettings");
			strcpy(resp1, "PlayMode");
			break;

		case SONOS_POSIT:
			pcmdbuf.print("GetPositionInfo");
			strcpy(resp1, "RelTime");
			break;

		case SONOS_TRACK:
			pcmdbuf.print("GetPositionInfo");
			strcpy(resp1, "dc:title>");
			strcpy(resp2, "dc:creator>");
			break;


		}
#ifdef DEBUG
check_mem();
Serial.flush();
#endif
//if ( cmd == SONOS_PLAY )return;
	if (client.connect(stue,1400)) {
//            delay(1200);
#ifdef DEBUG
		Serial.println("connected");
check_mem();
Serial.flush();

#endif
		/* output the command packet */
                pbuf.print("POST /MediaRenderer/");
                pbuf.print(service);
                pbuf.print("/Control HTTP/1.1\r\n");
		out(buf);
#ifdef DEBUG
                if ( client.getWriteError() > 0 )
                  Serial.println("grr" + client.getWriteError());
#endif                 
		out("Connection: close\r\n");
#ifdef DEBUG
                if ( client.getWriteError() > 0 )
                  Serial.println("grraaae"  + client.getWriteError());
#endif                 
                pbuf.begin();
                
		pbuf.print("Host: ");
                pbuf.print(stue[0]);
                pbuf.print(".");
                pbuf.print(stue[1]);
                pbuf.print(".");
                pbuf.print(stue[2]);
                pbuf.print(".");
                pbuf.print(stue[3]);
                pbuf.print(":1400\r\n");

		out(buf);
                pbuf.begin();
		
                pbuf.print("Content-Length: ");
                pbuf.print(269 + 2 * pcmdbuf.length() + pextra.length()   + pservice.length());
                pbuf.print("\r\n");
		out(buf);
                pbuf.begin();
                
		out("Content-Type: text/xml; charset=\"utf-8\"\r\n");
		pbuf.print("SOAPAction: \"urn:schemas-upnp-org:service:");
                pbuf.print(pservice);
                pbuf.print(":1#");
                pbuf.print(pcmdbuf);
                pbuf.print("\"\r\n");
		out(buf);
                pbuf.begin();
		out("\r\n");
#ifdef DEBUG
                Serial.println();
#endif		
                client << sonos_cmdh << "<u:" << cmdbuf << sonos_cmdp << service << sonos_cmdq << extra << "</u:" << cmdbuf << ">" << sonos_cmdf << "\r\n";
                
#ifdef DEBUG                
                Serial << sonos_cmdh << "<u:" << cmdbuf << sonos_cmdp << service << sonos_cmdq << extra << "</u:" << cmdbuf << ">" << sonos_cmdf << "\r\n";               
#endif

//                %s%s%s</u:%s>%s", SONOS_CMDH, cmdbuf, SONOS_CMDP, service, SONOS_CMDQ, extra, cmdbuf, SONOS_CMDF);
//		out(buf);
                pbuf.begin();
                
		/* wait for a response packet */
		timeout = millis();
		while ((!client.available()) && ((millis() - timeout) < 1000));

		/*
		 * parse the response looking for the strings in resp1 and
		 * resp2
		 */
#ifdef DEBUG
                Serial.println("Parsing resp");                
#endif                

		ptr1 = resp1;
		ptr2 = resp2;
                optr = resp1; //warning away
		copying = 0;
                int copycount = 0;
                int st = 0;
                char amp = 0;
		while (client.available()) {
			char            c = client.read();
#ifdef DEBUG
			Serial.print(c);
			//Serial.print(fsm[st]);
#endif

                      // FSM for parsing and converting &-entities and utf-8 in xml. See FSM.java. 
                      // Hack to loop back once if an ampersand is found, to allow for doubly encoded entities
                      while(c != fsm[st] && fsm[st+1] > 0 ) {
                        st += fsm[st+1];
                      }
                       if( fsm[st+2] > 0 ) {
                        c=fsm[st+2];
                        st=0;
                      } else if ( c == fsm[st] ) {
                        st+=3;  
                      } else {
                        if (amp==1)
                          c = '&';
                        //else if (st > 0)
                        //  c = '?';
                        st = 0;
                      }
                      //hack
                      if (amp == 0 && c == '&' && st != 3 ) {
                        st = 3;
                        amp = 1;
                      }
                      if(st > 0)
                         continue; 
                      amp = 0;  
            
Serial.print(c);

			/*
			 * if response buffers start with nulls, either no
			 * response required, or already received
			 */
			if (resp1[0] || resp2[0]) {
				/*
				 * if a response has been identified, copy
				 * the data
				 */
				if (copying) {
					/*
					 * look for the < character that
					 * indicates the end of the data
					 */
					if ( copycount >= MAX_DATA-1 ||
                                             c == '<' )
                                                 
                                                {
						/*
						 * stop receiving data, and
						 * null the first character
						 * in the response buffer
						 */
						//if (copying == 1)
						//	resp1[0] = 0;
						//else
						//	resp2[0] = 0;
						copying = 0;
    						  *optr = 0;

                                                  copycount = 0;
					} else {
						/*
						 * copy the next byte to the
						 * response buffer
						 */
						*optr = c;
						optr++;
                                                copycount++;
					}
				} else {
					/*
					 * look for input characters that
					 * match the response buffers
					 */
					if (c == *ptr1) {
						/*
						 * character matched -
						 * advance to next character
						 * to match
						 */
						ptr1++;

						/*
						 * is this the end of the
						 * response buffer
						 */
						if (*ptr1 == 0) {
							/*
							 * string matched -
							 * start copying from
							 * next character
							 * received
							 */
							copying = 1;
							optr = resp1;
							ptr1 = resp1;
						}
					} else
						ptr1 = resp1;

					/*
					 * as above for second response
					 * buffer
					 */
					if (c == *ptr2) {
						ptr2++;

						if (*ptr2 == 0) {
							copying = 2;
							optr = resp2;
							ptr2 = resp2;
						}
					} else
						ptr2 = resp2;
				}
			}

		}
	} else {
#ifdef DEBUG
		Serial.println("cfail");
#endif
	}
delay(200);
	client.stop();
#ifdef DEBUG
        Serial.println("bye");
#endif        

}

/* End of file */
/* ====================================================================== */
