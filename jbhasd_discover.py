# Simple Python3 script to 
# use zeroconf to discover JBHASD devices on the LAN
# and print their details and URLs
# waits a long time and exits

from zeroconf import ServiceBrowser, Zeroconf
import socket
import time
import requests
import sys
import os

http_timeout_secs = 5

class MyListener(object):  
    def remove_service(self, zeroconf, type, name):
        return

    def add_service(self, zeroconf, type, name):

        info = zeroconf.get_service_info(type, name)
        address = socket.inet_ntoa(info.address)
        port = info.port
        url = 'http://%s:%d/status' % (address, port)
        server = info.server
        print("Discovered: %s URL: %s" % (server, url))

        response = None
        api_session = requests.session()

        try:
            response = api_session.get(url)
        except:
            print("Error in urlopen (%s)" % (url))

        if response is not None:
            response_str = str(response.text)
            print("json len:%d \n%s" % (len(response_str), response_str))

        sys.stdout.flush()



zeroconf = Zeroconf()  
listener = MyListener()  
browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  
time.sleep(30)
zeroconf.close()

