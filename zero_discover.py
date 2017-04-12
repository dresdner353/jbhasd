# Simple Python3 script to 
# use zeroconf to discover JBHASD devices on the LAN
# and print their details and URLs

from six.moves import input  
from zeroconf import ServiceBrowser, Zeroconf
import socket


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
try:  
    input("Press enter to exit...\n\n")
finally:  
    zeroconf.close()
