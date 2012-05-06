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
#include <WebServer.h>
#include <EEPROM.h>
#include <Dns.h>

/*----------------------------------------------------------------------*/
/* Macros and constants */
/*----------------------------------------------------------------------*/


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


/* Arduino pin to which IR receiver is connected */
#define IR_PIN 8

/* IRremote hex codes for received remote commands */

#define REMOTE_PLAY     0xDD // mini
#define REMOTE_PAUSE    0x5D // mini pwr
#define REMOTE_NEXT     0x3D // mini
#define REMOTE_PREV     0x21





/* Enable DEBUG for serial debug output */
// not enough mem, must modularise
//#define DEBUG
//#define SOAPDEBUG

/* Sonos SOAP command packet skeleton */

FLASH_STRING(sonos_cmdh, "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>");
FLASH_STRING(sonos_cmdp, " xmlns:u=\"urn:schemas-upnp-org:service:");
FLASH_STRING(sonos_cmdq, ":1\"><InstanceID>0</InstanceID>");
FLASH_STRING(sonos_cmdf, "</s:Body></s:Envelope>");
FLASH_STRING(post_pre, "POST /MediaRenderer/");
FLASH_STRING(post_post, "/Control HTTP/1.1\r\n");
FLASH_STRING(soap_action, "SOAPAction: \"urn:schemas-upnp-org:service:");
FLASH_STRING(no_track,  "<no track>          ");
FLASH_STRING(no_artist, "<no artist>         ");



// see FSM.java
FLASH_STRING(fsm, "\x26\x3c\x0\x61\x18\x0\x6d\x9\x0\x70\x0\x0\x3b\x0\x26\x70\x0\x0\x6f\x0\x0\x73\x0\x0\x3b\x0\x27\x67\x9\x0\x74\x0\x0\x3b\x0\x3e\x6c\x9\x0\x74\x0\x0\x3b\x0\x3c\x71\x0\x0\x75\x0\x0\x6f\x0\x0\x74\x0\x0\x3b\x0\x22\xc3\x0\x0\x84\x3\x80\x85\x3\x81\x86\x3\x90\x96\x3\x86\x98\x3\x88\xa4\x3\xe1\xa5\x3\x84\xa6\x3\x91\xb6\x3\xef\xb8\x0\x89");


// Web Server
P(Page_start) = "<html><head><title>Sonos minicontroller</title><style type=\"text/css\"> BODY { font-family: sans-serif } H1 { font-size: 14pt; text-decoration: underline } P  { font-size: 10pt; }</style></head><body>\n";
P(Page_end) = "</body></html>";
P(Form_begin) = "<form action='/form' method='post'>Please enter the ZP IP:\n<input type='text' length='20' name='ip' id='ip' value='";
P(Form_end) = "'/><input type='submit' name='doit' value=' Store '/></form>";


/* IP address of ZonePlayer to control */
// will be read from EEPRom
IPAddress zp(0,0,0,0);
int valid = 0;

/* Millisecond timer values */
unsigned long   lastcmd = 0;
unsigned long   lastrew = 0;
unsigned long   lasttrackpoll =  0;
unsigned long   lastscroll =  0;

#define MAX_DATA 40
#define BUFLEN 80
/* Buffers used for Sonos data reception */
char            data1[MAX_DATA];
char            data2[MAX_DATA];

char            buf[BUFLEN];

int data1Chk = 0;
int data2Chk = 0;

/* Global null buffer used to disable data reception */
char            nullbuf[1] = {
  0};

/* IR receiver control */
IRrecv          irrecv(IR_PIN);
decode_results  results;

/* Ethernet control */
EthernetClient          client;

DNSClient DNS;

/* display */
SPI_VFD vfd(2, 3, 4);




/*----------------------------------------------------------------------*/
/* Function defintions */
/*----------------------------------------------------------------------*/


void helloCmd(WebServer &server, WebServer::ConnectionType type, char *url_tail, bool tail_complete)
{
#ifdef DEBUG
  Serial.println("main");
#endif  
  /* this line sends the standard "we're all OK" headers back to the
   browser */
  server.httpSuccess();

  /* if we're handling a GET or POST, we can output our data here.
   For a HEAD request, we just stop after outputting headers. */
  if (type == WebServer::HEAD)
    return;

  server.printP(Page_start);
  server.printP(Form_begin);
  zp.printTo(server);
  server.printP(Form_end);
  server.printP(Page_end);

}

void formCmd(WebServer &server, WebServer::ConnectionType type, char *url_tail, bool tail_complete)
{
#ifdef DEBUG
  Serial.println("form");
#endif
  if (type == WebServer::POST)
  {
    char name[2], value[20];
    server.readPOSTparam(name, 2, value, 20);
#ifdef DEBUG
    Serial.println("post");
    Serial.println(name);
    Serial.println(value);
#endif
    if (name[0] == 'i')
    {
      if ( DNS.getHostByName(value, zp) != 1 ) {
        zp = (uint32_t)0;
#ifdef DEBUG
        Serial.println("no go");
        Serial.println(value);
#endif
        valid = 0;
      } 
      else {
#ifdef DEBUG
        Serial.println("go");
        Serial.println(value);
#endif
        valid = 1;
        EEPROM.write(0,1);  
        EEPROM.write(1,zp[0]);  
        EEPROM.write(2,zp[1]);  
        EEPROM.write(3,zp[2]);  
        EEPROM.write(4,zp[3]);  
      }
    }


    server.httpSeeOther("/");
  }
  else
    helloCmd(server, type, url_tail, true);
}

void my_failCmd(WebServer &server, WebServer::ConnectionType type, char *url_tail, bool tail_complete)
{
  /* this line sends the standard "we're all screwed" headers back to the
   browser */
  server.httpFail();

  /* if we're handling a GET or POST, we can output our data here.
   For a HEAD request, we just stop after outputting headers. */
  if (type == WebServer::HEAD)
    return;

  server.printP(Page_start);

  server<<"error";
  server.printP(Page_end);

}


/* This creates an instance of the webserver.  By specifying a prefix
 * of "", all pages will be at the root of the server. */
WebServer webserver("", 80);

uint8_t * heapptr, * stackptr;
void check_mem() {
#if defined DEBUG or defined SOAPDEBUG 
  stackptr = (uint8_t *)malloc(4);          // use stackptr temporarily
  heapptr = stackptr;                     // save value of heap pointer
  free(stackptr);      // free up the memory again (sets stackptr to 0)
  stackptr =  (uint8_t *)(SP);           // save value of stack pointer
  Serial.println(stackptr - heapptr);
#endif
}

//void
//createChar(int n, _FLASH_ARRAY<byte> data ) {
//  byte createCharBuf[8];     
//  for ( int i = 0; i < 8; i++ )
//    createCharBuf[i] = data[i];
//  vfd.createChar( n, createCharBuf); 
//}


// clear eeprom first...
void readEEPROM() {
  valid = EEPROM.read(0);
  if ( valid != 0 ) {
    zp[0] = EEPROM.read(1);
    zp[1] = EEPROM.read(2);
    zp[2] = EEPROM.read(3);
    zp[3] = EEPROM.read(4);
  }
}

/*----------------------------------------------------------------------*/
/* setup - configures Arduino */


void 
setup()
{
  uint8_t         mac[6] = {
    0xBE, 0xEF, 0xEE, 0x00, 0x20, 0x09  };

  delay(1000);

  /* initialise Ethernet connection */
  Ethernet.begin(mac);

  DNS.begin(Ethernet.dnsServerIP());

  /* initialise IR receiver */
  irrecv.enableIRIn();

  // possibly find ip
  readEEPROM();


  vfd.begin(20, 2);
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    vfd.print(Ethernet.localIP()[thisByte], DEC);
    if ( thisByte < 3 ) vfd.print("."); 
  }
  vfd.setCursor(0,1); 
  vfd.print("-> ");
  if ( valid == 0 )
    vfd.print("(config me)");
  else {
    for (byte thisByte = 0; thisByte < 4; thisByte++) {
      // print the value of each byte of the IP address:
      vfd.print(zp[thisByte], DEC);
      if ( thisByte < 3 ) vfd.print("."); 
    }              
  }
  delay(2000);
//  createChar(1, char_aelig_lower);
//  createChar(2, char_oslash_lower);
//  createChar(3, char_aring_lower);
//  createChar(4, char_aelig_upper);
//  createChar(5, char_oslash_upper);
//  createChar(6, char_aring_upper);

  webserver.setDefaultCommand(&helloCmd);

  webserver.setFailureCommand(&my_failCmd);

  webserver.addCommand("form", &formCmd);

  webserver.begin();


#ifdef SOAPDEBUG
//delay(5000);
  Serial.begin(9600);
//  check_mem();
  Serial.flush();

#endif
#ifdef DEBUG
  Serial.begin(9600);
  check_mem();
  Serial.flush();

#endif
}

/*----------------------------------------------------------------------*/
/* loop - called repeatedly by Arduino */

char *pos1, *pos2;

void 
loop()
{
  // must have valid ip
  if ( valid != 0 ) {
    // poll song name/artist every second
    if ( millis() > lasttrackpoll + 1000) {
      sonos(SONOS_TRACK, data1, data2);
      if (data1[0] == -1 )
        strcpy(data1, "<no track>");
      if (data2[0] == -1 )
        strcpy(data2, "<no artist>");

      int d1 = sum_letters(data1);
      int d2 = sum_letters(data2);
      int first = 0;
      if ( d1 != data1Chk || d2 != data2Chk ) {
        vfd.clear();
        data1Chk = d1;
        data2Chk = d2;
        pos1 = data1; 
        pos2 = data2;
        first = 1;
      }
      lasttrackpoll = millis();

      // do manual scrolling. should check vfd.scrollDisplayLeft + ISR
      if ( first == 0 && strlen(data1) > 20 ) {
        if( ++pos1 == data1 - 19 + strlen(data1) )
          pos1 = data1;
      }
      if ( first == 0 && strlen(data2) > 20 ) {
        if( ++pos2 == data2 - 19 + strlen(data2) )
          pos2 = data2;
      }
      vfd.setCursor(0, 0);
      vfd.print(pos1);
      vfd.setCursor(0, 1);
      vfd.print(pos2);

#ifdef DEBUG
      Serial.println(data1);
      Serial.println(data2);
      Serial.println(pos1-data1);
      Serial.println(pos2-data2);
#endif    

    }



    /* look to see if a packet has been received from the IR receiver */
    if (irrecv.decode(&results)) {
#ifdef DEBUG
      Serial.println(results.value, HEX);
      check_mem();
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

//        case REMOTE_MODE:
//          sonos(SONOS_POSIT, data1, nullbuf);
//          vfd.setCursor(0, 1);
//          vfd.print("Posit: ");
//          vfd.print(data1);
//          break;

          /* store time at which last IR command was processed */
          lastcmd = millis();
        }
      }
      /* get ready to receive next IR command */
      irrecv.resume();
    }
  }
  int len = BUFLEN;

  /* process incoming connections one at a time forever */
  webserver.processConnection(buf, &len);
}

int 
sum_letters(char *str)
{
  int             tot = 0;
  char           *ptr = str;
  while (*ptr) {
    tot += *ptr;
    ptr++;
  }
  return tot;
}


/*----------------------------------------------------------------------*/
/* out - outputs supplied string to Ethernet client */

void 
out(const char *s)
{
  client.print(s);
#ifdef SOAPDEBUG
  Serial.print(s);
#endif
}




/*----------------------------------------------------------------------*/
/* sonos - sends a command packet to the ZonePlayer */

void 
sonos(int cmd, char *resp1, char *resp2)
{
  //char          buf[512];

  char            cmdbuf[32];
  char            extra[20];
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

  int connRes = 0;
  if (connRes = client.connect(zp,1400)) {
#ifdef DEBUG
    Serial.println("connected");
    check_mem();
    Serial.flush();

#endif
    /* output the command packet */
    pbuf << post_pre <<  service << post_post;
    out(buf);

    out("Connection: close\r\n");

    pbuf.begin();

    pbuf.print("Host: ");
    pbuf.print(zp[0]);
    pbuf.print(".");
    pbuf.print(zp[1]);
    pbuf.print(".");
    pbuf.print(zp[2]);
    pbuf.print(".");
    pbuf.print(zp[3]);
    pbuf.print(":1400\r\n");

    out(buf);
    pbuf.begin();

    pbuf.print("Content-Length: ");
    pbuf.print(269 + 2 * pcmdbuf.length() + pextra.length()   + pservice.length());
    pbuf.print("\r\n");
    out(buf);
    pbuf.begin();

    out("Content-Type: text/xml; charset=\"utf-8\"\r\n");
    pbuf << soap_action<<pservice<<":1#"<<pcmdbuf<<"\"\r\n";
    out(buf);
    pbuf.begin();
    out("\r\n");
	
    client << sonos_cmdh << "<u:" << cmdbuf << sonos_cmdp << service << sonos_cmdq << extra << "</u:" << cmdbuf << ">" << sonos_cmdf << "\r\n";

#ifdef SOAPDEBUG                
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
           

    ptr1 = resp1;
    ptr2 = resp2;
    optr = resp1; //warning away
    copying = 0;
    int copycount = 0;
    int st = 0;
    char amp = 0;
    char c1 = resp1[0];
    char c2 = resp2[0];
    while (client.available()) {
      char            c = client.read();
#ifdef SOAPDEBUG
      Serial.print(c);
      //Serial.print(fsm[st]);
#endif

      // FSM for parsing and converting &-entities and utf-8 in xml. See FSM.java. 
      // Hack to loop back once if an ampersand is found, to allow for doubly encoded entities
      while(c != fsm[st] && fsm[st+1] > 0 ) {
        st += fsm[st+1];
      }
      if( fsm[st+2] != 0 ) {
        c=fsm[st+2];
        st=0;
      } 
      else if ( c == fsm[st] ) {
        st+=3;  
      } 
      else {
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


#ifdef SOAPDEBUG            
      Serial.print(c);
#endif

      if (c1 || c2) {
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
            //else if( copying == 2)
            //	resp2[0] = 0;
            if (copying == 1)
              c1 = 0;
            else if( copying == 2)
              c2 = 0;
            copying = 0;
            *optr = 0;

            copycount = 0;
          } 
          else {
            /*
						 * copy the next byte to the
             						 * response buffer
             						 */
            *optr = c;
            optr++;
            copycount++;
          }
        } 
        else {
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
          } 
          else
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
          } 
          else
            ptr2 = resp2;
        }
      }

      // indicate "nothing found"

    }
    if (c1 != 0)
      resp1[0] = -1;
    if ( c2 != 0 )
      resp2[0] = -1;
  } 
  else {
    strcpy(resp1, "Connection error");
    sprintf(resp2, "code %d", connRes);
    
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

