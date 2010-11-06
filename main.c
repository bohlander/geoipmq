/*
    This is free software; you can redistribute it and/or modify it under the
    terms of the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your option)
    any later version.

    This software is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABIL-
    ITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
    Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
    =========================================================================
*/

#include <zmq.h>
#include <GeoIP.h>
#include <GeoIPCity.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

#define MAX_BUFFER 250

GeoIP* init_geoip(const char* filename) {
  return GeoIP_open(filename, GEOIP_STANDARD);  
}

int resolve_ip(GeoIP* geoip, const char* ipaddr, char* out, int max_out) {
  GeoIPRecord *r = GeoIP_record_by_addr(geoip, ipaddr);
  int chars_written = 0;
  if (NULL == r) {
    chars_written = snprintf(out, max_out, "NOT_FOUND");
  }
  else {
    chars_written  = snprintf(out, max_out, "%s\t%s\t%s\t%s\t%f\t%f\t%d\t%d", r->country_code, r->region, r->city, r->postal_code,
			      r->latitude, r->longitude, r->metro_code, r->area_code);

    GeoIPRecord_delete(r);
  }
  return chars_written;
}



int main (int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: geoip_server geoipdb port");
    return 0;
  }

  GeoIP* gi = init_geoip(argv[1]);
  if (gi == NULL) {
    fprintf(stderr, "Could not open geoip database %s", argv[1]);
    return 0;
  }
  void *context = zmq_init(1);

  //  Socket to talk to clients
  void *responder = zmq_socket(context, ZMQ_REP);

  // Check port
  char port[25];
  if (strlen(argv[2]) > 6 || atoi(argv[2]) == 0) {
    fprintf(stderr, "Invalid port %s", argv[2]);
  }
  
  sprintf(port, "tcp://*:%s", argv[2]);
  zmq_bind (responder, port);

  while (1) {
    char input[MAX_BUFFER];
    //  Wait for next request from client
    zmq_msg_t request;
    zmq_msg_init(&request);
    zmq_recv(responder, &request, 0);

    // Copy and null terminate incoming message
    int size = zmq_msg_size(&request);
    if (size + 1 >= MAX_BUFFER ) {
      goto bad_message;
    }

    char* req_data = (char*) zmq_msg_data(&request);
    strncpy(input, req_data, size);
    input[size] = 0;
    zmq_msg_close(&request);

    // Process GeoIP request or fail.
    if (strncmp(input, "geoip ", 6) == 0) {
      char output[MAX_BUFFER];
      int bytes = resolve_ip(gi, input + 6, output, MAX_BUFFER);
      
      zmq_msg_t reply;
      zmq_msg_init_size(&reply, bytes);
      memcpy((void*)zmq_msg_data(&reply), output, bytes);
      zmq_send(responder, &reply, 0);
      zmq_msg_close(&reply);

    }
    else {
      goto bad_message;
    }
    continue;


  bad_message:
    // Bail, invalid input.
    zmq_msg_close (&request);
    zmq_msg_t reply;
    zmq_msg_init_size (&reply, 6);
    memcpy ((void *) zmq_msg_data (&reply), "ERROR", 5);
    zmq_send (responder, &reply, 0);
    zmq_msg_close (&reply);      

  }
  zmq_term (context);
  return 0;
}
