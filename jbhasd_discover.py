# Simple Python3 script to 
# use zeroconf to discover JBHASD devices on the LAN
# and print their details and URLs
# waits a long time and exits

from zeroconf import ServiceBrowser, Zeroconf
import socket
import time
import requests
import sys

url_set = set()
http_timeout_secs = 2

class MyListener(object):  
    def remove_service(self, zeroconf, type, name):
        print("\nService %s removed" % (name))
        return

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        address = socket.inet_ntoa(info.address)
        port = info.port
        url = 'http://%s:%d' % (address, port)
        server = info.server
        url_set.add(url)
        print("\nDiscovered: %s URL: %s" % (server, url))

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

while 1:
    time.sleep(5)
