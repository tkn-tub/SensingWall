import base64
import json
import math
import re
import socket
import struct
import subprocess
import time
from typing import Optional
from datetime import datetime

import influxdb_client
from influxdb_client import WritePrecision, WriteOptions
from influxdb_client.client.write_api import SYNCHRONOUS
from influxdb_client.rest import ApiException

from wait_timer import WaitTimer

import multiprocessing

num_processes = multiprocessing.cpu_count()

host_ip = "192.168.4.11"
host_port = 4951

# Variables to store CSI statistics
packet_count = multiprocessing.Value('i', 0)
total_packet_counts = multiprocessing.Value('i', 0)

# set timer for print-Interval
print_stats_wait_timer= WaitTimer(1.0)

bucket = "csi"
org = "my-init-org"
#token = "XNu7o7FCokJgHk5dnsbD9OpwKfVydwzeqnM18lTLsZU5HeScbmaOVY28g_4E8H0PzfFqMjUR27J-DWHu3Qrtnw=="
# token = "NPawVKiFSNoMnBMHpGiECk0wN13-cXTodw4WbcaxdqsWmSRg97uwuFw85Td5btU7Ar_MKw0PHrFgzj4Ljcb7tA=="
token = "IWSYv4r7uMLhVgRqxA64_dF0uboUNMNp2xABWGaQ20dG7jq78ynjOKvvESz6UUOYVAPtTeQAPJMmjw6Fzf-yjQ=="
# Store the URL of your InfluxDB instance
url = "http://localhost:8086"


def get_sig_mode(value: int) -> str:
    if value == 0:
        return 'non HT(11bg)'
    elif value == 1:
        return 'HT(11n)'
    elif value == 3:
        return 'VHT(11ac)'
    else:
        raise ValueError('Unknown sig_mode')


def get_channel_bandwidth(value: int) -> str:
    if value == 0:
        return '20MHz'
    elif value == 1:
        return '40MHz'
    else:
        raise ValueError('Unknown cwb')


def get_aggregation(value: int) -> str:
    if value == 0:
        return 'MPDU'
    elif value == 1:
        return 'AMPDU'
    else:
        raise ValueError('Unknown aggregation')


def get_secondary_channel(value: int) -> str:
    if value == 0:
        return 'none'
    elif value == 1:
        return 'above'
    elif value == 2:
        return 'below'
    else:
        raise ValueError('Unknown secondary_channel')


def create_records_from_raw_data(data_dict: dict):
    imaginary = []
    real = []
    rssi = data_dict['rssi']
    rssi_scale_factor = pow(10, rssi / 20.0)
    csi_data = data_dict['csi_raw']
    for i, val in enumerate(csi_data):
        if i % 2 == 0:
            imaginary.append(val)
        else:
            real.append(val)

    csi_len = data_dict['csi_len']
    amplitudes = []
    phases = []
    if len(imaginary) > 0 and len(real) > 0:
        for j in range(int(csi_len / 2)):
            amplitudes.append(round(math.sqrt(imaginary[j] ** 2 + real[j] ** 2), 2))
            phases.append(round(math.atan2(imaginary[j], real[j]), 2))

    return [{
        "measurement": "csi",
        "time": data_dict['real_timestamp'],
        "tags": {
            "source": data_dict['mac'],
            "destination": data_dict['dnode'],
            "mcs": data_dict['mcs'],
            "sig_mode": get_sig_mode(data_dict['sig_mode']),
            "channel_bandwidth": get_channel_bandwidth(data_dict['cwb']),
            "aggregation": get_aggregation(data_dict['aggregation']),
            "channel": data_dict['channel'],
            "secondary_channel": get_secondary_channel(data_dict['secondary_channel']),
            "noise_floor": data_dict['noise_floor'],
            "sc_ind_start": data_dict['sc_ind_start'],
            "subcarrier": data_dict['sc_ind_start'] + i,
        },
        "fields": {'amplitude': amplitudes[i], 'phase': phases[i], 'amplitude_rssi_scaled': rssi_scale_factor * amplitudes[i], 'rssi': rssi}
    } for i in range(int(csi_len / 2))]


def get_udp_stats() -> Optional[dict]:
    # Run the netstat command and capture the output
    result = subprocess.check_output(["netstat", "-s", "--udp"], universal_newlines=True)

    # Split the output into lines
    lines = result.split('\n')

    udp_stats = {}
    inside_udp_section = False

    pattern = re.compile(r"^(\d+)\s+([a-zA-Z\s]+)$")

    # Iterate through the lines to find and extract UDP stats
    for line in lines:
        if line.startswith("Udp:"):
            inside_udp_section = True
            continue
        if inside_udp_section:
            try:
                value, key = pattern.search(line.strip()).group(1, 2)
                key = key.strip()
                value = value.strip()
                udp_stats[key] = int(value)
            except:
                break

    if not udp_stats:
        return None

    return {
        "measurement": "udp",
        "time": round(time.time() * 1e6),
        "fields": {**udp_stats}
    }


client = influxdb_client.InfluxDBClient(
    url=url,
    token=token,
    org=org,
    enable_gzip=True
)

buckets_api = client.buckets_api()

try:
    buckets_api.create_bucket(
        bucket_name=bucket,
        org=org,
        retention_rules=[{
            "type": "expire",
            "everySeconds": 60 * 60,
            "shardGroupDurationSeconds": 0
        }],
    )
except ApiException as e:
    if e.status == 422:
        # probably bucket already exists
        pass
    else:
        raise e

write_api = client.write_api(write_options=WriteOptions(batch_size=50_000, flush_interval=10_000))

while True:
    try:
        sock_fd = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM, proto=0)
        sock_fd.bind((host_ip, host_port))
        break
    except Exception as error:
        print(error)
        print('Failed to create a socket, retrying...')
        time.sleep(5)

def process_packet(queue, p_count):

    # Wait Timers.
    print_stats_wait_timer_for_process = WaitTimer(0.5)
    db_write_wait_timer = WaitTimer(0.2)

    records_list = []
    rates_list = []
    
    while True:
        try:
            data = queue.get(block=True)
            pid = multiprocessing.current_process().pid

            data = json.loads(data)
            data['real_timestamp'] = round(time.time() * 1e6)
            if 'csi_raw' in data:
                data['csi_raw'] = list(struct.unpack('b' * data['csi_len'], base64.b64decode(data['csi_raw'], validate=True)))

            records = create_records_from_raw_data(data)
            records_list.extend(records)

            if print_stats_wait_timer_for_process.check():
                print_stats_wait_timer_for_process.update()
                rates_list.append({"measurement": "packet_rate", "time": round(time.time() * 1e6), "fields": {"rate": p_count.value}})

            if db_write_wait_timer.check():
                write_api.write(bucket=bucket, org=org, record=records_list, write_precision=WritePrecision.US)
                write_api.write(bucket=bucket, org=org, record=rates_list, write_precision=WritePrecision.US)
                stats = get_udp_stats()
                if stats:
                    write_api.write(bucket=bucket, org=org, record=stats, write_precision=WritePrecision.US)
                records_list = []
                rates_list = []
                db_write_wait_timer.update()
        except Exception as ex:
            print(ex)
            # just sleep before trying again
            print('An error occurred, retrying...')
            #time.sleep(5)

# create queue to pass arguments zu processes
queue = multiprocessing.Queue()

# Create new processes
# one process is for receiving packages and put them in the queue
for i in range(int(num_processes / 2)):
    multiprocessing.Process(target=process_packet, args=(queue,packet_count)).start()

while True:
    data, _ = sock_fd.recvfrom(1024)
    queue.put(data)

    packet_count.value += 1
    total_packet_counts.value += 1

    if print_stats_wait_timer.check():
        print_stats_wait_timer.update()
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}]: Packet Count:", packet_count.value, "per second.", "Total Count:", total_packet_counts.value)
        print("Packets in Queue: ", queue.qsize())
        packet_count.value = 0
