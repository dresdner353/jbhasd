# Simple Python3 script to 
# use zeroconf to discover JBHASD devices on the LAN
# and print their details and URLs
# waits a long time and exits

from six.moves import input  
from zeroconf import ServiceBrowser, Zeroconf
import socket
import time

class MyListener(object):  
    def remove_service(self, zeroconf, type, name):
        print("\nService %s removed" % (name))

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        address = socket.inet_ntoa(info.address)
        port = info.port
        print("\nDiscovered: %s\nURL: http://%s:%d\nInfo:%s\n" % (name, address, port, info))

zeroconf = Zeroconf()  
listener = MyListener()  
browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

while 1:
    time.sleep(5)
