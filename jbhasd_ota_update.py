# Simple Python3 script to 
# use zeroconf to discover JBHASD devices on the LAN
# and print their details and URLs
# waits a long time and exits

from six.moves import input  
from zeroconf import ServiceBrowser, Zeroconf
import socket
import time
import urllib
import urllib.parse
import urllib.request
import urllib.error
import argparse
import json
import os

def fetch_url(url, url_timeout, parse_json):
    # General purpoe URL fetcher
    # return contents of page and parsed as json
    # if the parse_json arg is 1
    response_str = None

    #print("%s Fetching URL:%s, timeout:%d" % (time.asctime(), url, url_timeout)) 

    response = None
    try:
        response = urllib.request.urlopen(url,
                                          timeout = url_timeout)
    except:
        print("%s Error in urlopen URL:%s" % (time.asctime(), url))
 
    if response is not None:
        try:
            response_str = response.read()
        except:
            print("%s Error in response.read() URL:%s" % (time.asctime(), url))
            return None

        if parse_json:
            try:
                json_data = json.loads(response_str.decode('utf-8'))
            except:
                print("%s Error in JSON parse.. URL:%s Data:%s" % (time.asctime(),
                                                                   url, 
                                                                   response_str))
                return None
            return json_data

    return response_str
     


ip_set = set()

class MyListener(object):  
    def remove_service(self, zeroconf, type, name):
        return

    def add_service(self, zeroconf, type, name):
        global ip_set

        info = zeroconf.get_service_info(type, name)
        if info is not None:
            address = socket.inet_ntoa(info.addresses[0])
            ip_set.add(address)
        return

parser = argparse.ArgumentParser(
        description='JBHASD OTA Updater')

parser.add_argument('--flash_size', 
                    help = 'Flash size', 
                    type = int,
                    required = True)

parser.add_argument('--firmware', 
                    help = 'Firmware', 
                    required = True)

args = vars(parser.parse_args())

flash_size = args['flash_size']
firmware = args['firmware']



zeroconf = Zeroconf()  
listener = MyListener()  
browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

discovery_secs = 60
http_timeout_secs = 5

print("Giving %d seconds for discovery.." % (discovery_secs))
time.sleep(discovery_secs)

print("Discovered %d devices in total" % (len(ip_set)))
flashed_total = 0
for ip in ip_set:
    url = 'http://%s' % (ip)
    print( "Discovered.. %s" % (url))

    # get JSON data to check flash size
    json_data = fetch_url(url, http_timeout_secs, 1)
    if json_data is not None:
        name = json_data['name']
        zone = json_data['zone']
        device_flash_size = json_data['system']['flash_size']
        print( "Name:%s Zone:%s Flash:%d" % (name, zone, device_flash_size))
        if device_flash_size == flash_size:
            # Flash
            print("OTA Updating..")
            ota_cmd = "espota.py -i %s -f %s" % (ip, firmware)
            print(ota_cmd)
            os.system(ota_cmd)
            flashed_total += 1

print("Flashed %d devices in total" % (flashed_total))
