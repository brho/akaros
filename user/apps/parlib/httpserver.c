#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"

#include "lwip/mem.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/inet_chksum.h"
#include <lwip/tcpip.h>
#include <netif/ethernetif.h>


#if LWIP_NETCONN

const static char http_html_hdr[] = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
const static char http_index_html[] = "<html><head><title>It's ROS baby!</title></head><body><h1>Welcome to our ROS webserver!</h1><p>(Please don't root us)<pre>\n             .-.  .-.\n             |  \\/  |\n            /,   ,_  `'-.\n          .-|\\   /`\\     '. \n        .'  0/   | 0\\  \\_  `\".  \n     .-'  _,/    '--'.'|#''---'\n      `--'  |       /   \\#\n            |      /     \\#\n            \\     ;|\\    .\\#\n            |' ' //  \\   ::\\# \n            \\   /`    \\   ':\\#\n             `\"`       \\..   \\#\n                        \\::.  \\#\n                         \\::   \\#\n                          \\'  .:\\#\n                           \\  :::\\#\n                            \\  '::\\#\n                             \\     \\#\n                              \\:.   \\#\n                               \\::   \\#\n                                \\'   .\\#\n                             jgs \\   ::\\#\n                                  \\      \n</pre></body></html>";

struct ip_addr ipaddr, netmask, gw;
struct netif netif;

int http_server();

void network_init()
{
        printf("Starting up network stack....\n");

        IP4_ADDR(&gw, 0,0,0,0);
        IP4_ADDR(&ipaddr, 0,0,0,0);
        IP4_ADDR(&netmask, 0,0,0,0);

        netif.name[0] = 'P';
        netif.name[1] = 'J';

        tcpip_init(NULL, NULL);

        netif_add(&netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input);

        netif_set_default(&netif);

        dhcp_start(&netif);

        printf("Done...\n");

}


int main(int argc, char** argv) {

        static struct ip_addr ping_addr;

        IP4_ADDR(&ping_addr, 192,168,0,3);

        network_init();

	printf("Starting http server\n");

	http_server();

        return 0;

}



void http_server_serve(struct netconn *conn) {
  struct netbuf *inbuf;
  char *buf;
  u16_t buflen;

  /* Read the data from the port, blocking if nothing yet there. 
   We assume the request (the part we care about) is in one netbuf */
  inbuf = netconn_recv(conn);
  
  if (netconn_err(conn) == ERR_OK) {
    netbuf_data(inbuf, (void**)&buf, &buflen);
    
    /* Is this an HTTP GET command? (only check the first 5 chars, since
    there are other formats for GET, and we're keeping it very simple )*/
    if (buflen>=5 &&
        buf[0]=='G' &&
        buf[1]=='E' &&
        buf[2]=='T' &&
        buf[3]==' ' &&
        buf[4]=='/' ) {
      
      /* Send the HTML header 
             * subtract 1 from the size, since we dont send the \0 in the string
             * NETCONN_NOCOPY: our data is const static, so no need to copy it
       */
      netconn_write(conn, http_html_hdr, sizeof(http_html_hdr)-1, NETCONN_NOCOPY);
      
      /* Send our HTML page */
      netconn_write(conn, http_index_html, sizeof(http_index_html)-1, NETCONN_NOCOPY);
    }
  }

  /* Close the connection (server closes in HTTP) */
  netconn_close(conn);
  /* Delete the buffer (netconn_recv gives us ownership,
   so we have to make sure to deallocate the buffer) */
  netbuf_delete(inbuf);
}

int http_server() {
  struct netconn *conn, *newconn;

  /* Create a new TCP connection handle */
  conn = netconn_new(NETCONN_TCP);
  LWIP_ERROR("http_server: invalid conn", (conn != NULL), return -1;);

  /* Bind to port 80 (HTTP) with default IP address */
  netconn_bind(conn, NULL, 9999);

  /* Put the connection into LISTEN state */
  netconn_listen(conn);

  while(1) {
	
    newconn = netconn_accept(conn);

    http_server_serve(newconn);

    netconn_delete(newconn);

  }
  return 0;
}

#endif /* LWIP_NETCONN*/
