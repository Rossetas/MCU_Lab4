import serial
import time
import json
import paho.mqtt.client as mqtt

# Configuración del puerto serial
SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 9600

# Configuración de ThingsBoard
THINGSBOARD_HOST = 'iot.eie.ucr.ac.cr'
ACCESS_TOKEN = 'hpogmawb2dy9gd61sjfq'  # Token de dispositivo en ThingsBoard
MQTT_PORT = 1883

# Inicializar conexión MQTT
client = mqtt.Client()
client.username_pw_set(ACCESS_TOKEN)
client.connect(THINGSBOARD_HOST, MQTT_PORT, 60)
client.loop_start()

# Función para parsear los datos del STM32
def parse_data(line):
    try:
        parts = line.strip().split('\t')
        if len(parts) != 5:
            return None
        return {
            "X_axis": float(parts[0]),
            "Y_axis": float(parts[1]),
            "Z_axis": float(parts[2]),
            "battery_level": float(parts[3]),
            "communication": 1 if parts[4].strip() == "ON" else 0
        }
    except:
        return None

# Leer del puerto serial y enviar a ThingsBoard
with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) as ser:
    time.sleep(2)  # Tiempo para que el puerto se estabilice
    print("Leyendo datos del puerto serial y enviando a ThingsBoard...")
    while True:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8').strip()
            print("Datos recibidos:", line)
            data = parse_data(line)
            if data:
                telemetry = json.dumps(data)
                client.publish('v1/devices/me/telemetry', telemetry)
                print("Enviado a ThingsBoard:", telemetry)
        time.sleep(0.5)
