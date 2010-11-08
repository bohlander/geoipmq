require 'rubygems';
require 'zmq'; 
z = ZMQ::Context.new; 
requester = z.socket(ZMQ::REQ); 
requester.connect("tcp://127.0.0.1:7555"); 
require 'ipaddr'; 
requester.send("geoip 24.2.2.1")
p = requester.recv
puts("received #{p}")
1.upto(100000) { ipv4 = IPAddr.new(rand(2**32),Socket::AF_INET); requester.send("geoip #{ipv4}"); s = requester.recv;  }
