import influxdb_client
from influxdb_client.client.write_api import SYNCHRONOUS

bucket = "csi"
org = "my-init-org"

token = "AzuDFnC_AgC1sK-fiq0NZtT1UwHqO1QLuFplP6QXifqPk0ps_-uKeH5c1Z1DqoePL7qcGH4E-3LNTNmpN8Uqiw=="

# Store the URL of your InfluxDB instance
url = "http://130.149.49.35:8086"

def get_sta_mac_addr(client):
    # get all station mac addr

    query_api = client.query_api()
    query = 'from(bucket: "csi")\
      |> range(start: -1m)\
      |> filter(fn: (r) => r["_measurement"] == "csi" and r.source != "48:27:e2:3b:33:2d")\
      |> map(fn: (r) => ({_value: r.source}))\
      |> distinct()\
      |> yield(name: "mac_addresses")'

    result = query_api.query(org=org, query=query)
    print(result)

    stas = []
    for table in result:
        for record in table.records:
            rv = record.values['_value']
            stas.append(rv)

    return stas

def get_sc_amplitude(sta_mac_addr, sc_id):

    query_api = client.query_api()
    query = 'from(bucket: "csi")\
      |> range(start: -2m)\
      |> filter(fn: (r) => r["_measurement"] == "csi" and r["_field"] == "amplitude" and r["destination"] == "' + sta_mac_addr + '" and r._value != 0 and r["subcarrier"] == "' + sc_id + '")\
      |> map(fn: (r) => ({_value: r._value, _time: r._time, subcarrier: r.subcarrier}))\
      |> aggregateWindow(every: 10s, fn: mean, createEmpty: false)\
      |> yield(name: "dl_amp_sta1")'

    result = query_api.query(org=org, query=query)
    print(result)

    amp = []
    for table in result:
        for record in table.records:
            ts = record.values['_time']
            rv = record.values['_value']
            #print(f'{ts}: => {rv}')
            amp.append(rv)

    return amp

if __name__ == '__main__':
    client = influxdb_client.InfluxDBClient(
        url=url,
        token=token,
        org=org
    )

    stas = get_sta_mac_addr(client)

    print(f'#STAs: {len(stas)}')
    print(stas)

    sc_id = "-41"
    for sta_mac_addr in stas:
        amp = get_sc_amplitude(sta_mac_addr, sc_id)

        print(f'STA: {sta_mac_addr}, #samples: {len(amp)}')
        print(amp)
