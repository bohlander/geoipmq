/*
Copyright (c) <2010> Mike Bohlander

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <zmq.h>
#include <GeoIP.h>
#include <GeoIPCity.h>

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

#define MAX_BUFFER 250

void log_error(const char* error, ...) {
  va_list args;
  va_start(args,error);
  vfprintf(stderr, error,args);
  va_end(args);
}

GeoIP* init_geoip(const char* filename)
{
  return GeoIP_open(filename, GEOIP_STANDARD);  
}

int resolve_ip(GeoIP* geoip, const char* ipaddr, char* out, int max_out)
{
  GeoIPRecord *r = GeoIP_record_by_addr(geoip, ipaddr);
  int chars_written = 0;
  if (r == NULL) {
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
    log_error("Usage: geoip_server geoipdb port\n");
    return 1;
  }

  GeoIP* gi = init_geoip(argv[1]);
  if (gi == NULL) {
    log_error("Could not open geoip database %s\n", argv[1]);
    return 1;
  }
  void *context = zmq_init(1);
  if (context == NULL) {
    log_error("Could not initialize ZMQ\n");
    return 1;
  }

  //  Socket to talk to clients
  void *responder = zmq_socket(context, ZMQ_REP);
  if (responder == NULL) {
    log_error("Could not initialize ZMQ responder\n");
    return 1;
  }

  // Check port
  char port[25];
  if (strlen(argv[2]) > 6 || atoi(argv[2]) == 0) {
    log_error("Invalid port %s", argv[2]);
    return 1;
  }
  
  int rc;
  sprintf(port, "tcp://*:%s", argv[2]);
  rc = zmq_bind (responder, port);
  assert(rc == 0);

  while (1) {
    char input[MAX_BUFFER];
    //  Wait for next request from client
    zmq_msg_t request;
    rc = zmq_msg_init(&request);    
    assert(rc == 0);
    rc = zmq_recv(responder, &request, 0);
    assert(rc == 0);

    int size = zmq_msg_size(&request);
    if (size + 1 >= MAX_BUFFER ) {
      // treat this as an error, this message is too large to be a valid geoip request.
      goto bad_message;
    }

    // Copy and null terminate incoming message
    char* req_data = (char*) zmq_msg_data(&request);
    strncpy(input, req_data, size);
    input[size] = 0;
    zmq_msg_close(&request);

    // Process GeoIP request or fail.
    if (strncmp(input, "geoip ", 6) == 0) {
      char output[MAX_BUFFER];
      int bytes = resolve_ip(gi, input + 6, output, MAX_BUFFER);
      
      zmq_msg_t reply;
      rc = zmq_msg_init_size(&reply, bytes);
      assert(rc == 0);
      memcpy((void*)zmq_msg_data(&reply), output, bytes);
      rc = zmq_send(responder, &reply, 0);
      assert(rc == 0);
      zmq_msg_close(&reply);

    }
    else {
      goto bad_message;
    }
    continue;


  bad_message:
    // Bail, invalid input.
    rc = zmq_msg_close (&request);
    assert(rc == 0);
    zmq_msg_t reply;
    rc = zmq_msg_init_size(&reply, 6);
    assert(rc == 0);
    memcpy((void *) zmq_msg_data (&reply), "ERROR", 5);
    rc = zmq_send(responder, &reply, 0);
    assert(rc == 0);
    rc = zmq_msg_close (&reply);      
    assert(rc == 0);
  }
  zmq_term (context);
  return 0;
}
