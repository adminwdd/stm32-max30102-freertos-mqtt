import certifi
import json
import queue
import ssl

import matplotlib.animation as animation
import matplotlib.pyplot as plt
import paho.mqtt.client as mqtt


BROKER_HOST = "your_broker_host_here"
BROKER_PORT = 8883
MQTT_USERNAME = "your_username"
MQTT_PASSWORD = "your_password"

PPG_TOPIC = "/ppg/data"
STATUS_TOPIC = "/ppg/status"

frame_queue = queue.Queue()

MAX_POINTS = 500
UPDATE_CHUNK = 100

ppg_buffer = []
current_valley_index = []
current_heartrate = None
esp32_status = "-1"


def on_connect(client, userdata, flags, reason_code, properties):
    print("Connected:", reason_code)

    if reason_code.is_failure:
        print("连接失败:", reason_code)
        return

    client.subscribe([
        (PPG_TOPIC, 1),
        (STATUS_TOPIC, 1),
    ])

    print("已订阅 PPG topic:", PPG_TOPIC)
    print("已订阅状态 topic:", STATUS_TOPIC)


def on_subscribe(client, userdata, mid, reason_codes, properties):
    print("订阅确认 mid:", mid, "reason_codes:", reason_codes)


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    print("MQTT 已断开:", reason_code)


def handle_status_message(raw, msg):
    global esp32_status

    try:
        data = json.loads(raw)
        client_id = data.get("client_id", "unknown")
        status = data.get("status", "unknown")
        esp32_status = status

        print(
            f"[ESP32状态] client_id:{client_id}, "
            f"status:{status}, "
            f"retain:{msg.retain}"
        )
    except Exception as exc:
        print("[ESP32状态] JSON解析失败:", exc)
        print("RAW:", raw)


def handle_ppg_message(raw):
    try:
        data = json.loads(raw)

        heartrate = data.get("heartrate")
        ppg_data = [int(i) for i in data.get("ppg", [])]
        valley_count = int(data.get("valley_count", 0))
        valley_index = [int(i) for i in data.get("valley_index", [])[:valley_count]]

        if not ppg_data:
            print("PPG数据为空")
            return

        print(
            f"心率:{heartrate}, PPG长度:{len(ppg_data)}, "
            f"min:{min(ppg_data)}, max:{max(ppg_data)}, "
            f"valley_count:{valley_count}, valley_index:{valley_index}"
        )

        frame_queue.put({
            "heartrate": heartrate,
            "ppg": ppg_data,
            "valley_count": valley_count,
            "valley_index": valley_index,
        })

    except Exception as exc:
        print("PPG数据解析或格式错误:", exc)
        print("RAW:", raw[:100])


def on_message(client, userdata, msg):
    raw = msg.payload.decode("utf-8", errors="ignore")

    if msg.topic == STATUS_TOPIC:
        handle_status_message(raw, msg)
        return

    if msg.topic == PPG_TOPIC:
        handle_ppg_message(raw)
        return

    print("收到未知 topic:", msg.topic)
    print("RAW:", raw[:100])


fig, ax = plt.subplots(figsize=(10, 5))
line, = ax.plot([], [], lw=1.5, color="b", label="PPG")
valley_line, = ax.plot([], [], "ro", markersize=6, label="Valley")

ax.set_xlim(0, MAX_POINTS)
ax.set_ylim(-50000, 20000)
ax.set_title("Real-Time PPG Signal")
ax.set_xlabel("Sample Points")
ax.set_ylabel("PPG Amplitude")
ax.grid(True)
ax.legend(loc="upper right")


def update(frame):
    global ppg_buffer
    global current_valley_index
    global current_heartrate
    global esp32_status

    updated = False

    while True:
        try:
            frame_data = frame_queue.get_nowait()
        except queue.Empty:
            break

        new_chunk = frame_data["ppg"]
        current_valley_index = frame_data["valley_index"]
        current_heartrate = frame_data["heartrate"]

        if len(ppg_buffer) < MAX_POINTS:
            ppg_buffer.extend(new_chunk)
            if len(ppg_buffer) > MAX_POINTS:
                ppg_buffer = ppg_buffer[-MAX_POINTS:]
        else:
            ppg_buffer = ppg_buffer[UPDATE_CHUNK:] + new_chunk

        updated = True

    if updated and ppg_buffer:
        x_data = list(range(len(ppg_buffer)))
        line.set_data(x_data, ppg_buffer)
        window_start = MAX_POINTS - len(ppg_buffer)
        valley_x = []
        valley_y = []

        for idx in current_valley_index:
            display_idx = idx - window_start
            if 0 <= display_idx < len(ppg_buffer):
                valley_x.append(display_idx)
                valley_y.append(ppg_buffer[display_idx])

        valley_line.set_data(valley_x, valley_y)

        current_min = min(ppg_buffer)
        current_max = max(ppg_buffer)
        margin = (current_max - current_min) * 0.1 if current_max != current_min else 10
        ax.set_ylim(current_min - margin, current_max + margin)

    if current_heartrate is not None:
        ax.set_title(
            f"Real-Time PPG Signal    "
            f"HR: {current_heartrate} bpm    "
            f"ESP32: {esp32_status}"
        )
    else:
        ax.set_title(f"Real-Time PPG Signal    ESP32: {esp32_status}")

    return line, valley_line


client = mqtt.Client(
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
    protocol=mqtt.MQTTv5,
)

client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message
client.on_subscribe = on_subscribe

client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
client.tls_set(
    ca_certs=certifi.where(),
    cert_reqs=ssl.CERT_REQUIRED,
    tls_version=ssl.PROTOCOL_TLS_CLIENT,
)
client.tls_insecure_set(False)
client.connect(BROKER_HOST, BROKER_PORT, 60)
client.loop_start()

ani = animation.FuncAnimation(
    fig,
    update,
    interval=20,
    blit=False,
    cache_frame_data=False,
)

try:
    plt.show()
finally:
    client.loop_stop()
    client.disconnect()
