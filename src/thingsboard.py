import serial
import csv
import time
from datetime import datetime
import paho.mqtt.client as mqtt

# Configuración del puerto serial
PORT = '/dev/ttyACM0'
BAUDRATE = 115200
CSV_FILENAME = 'logs.csv'

# Configuración de MQTT
MQTT_BROKER = "iot.eie.ucr.ac.cr"
ACCESS_TOKEN = "hpogmawb2dy9gd61sjfq"
MQTT_PORT = 1883
TOPIC = "v1/devices/me/telemetry"

def main():
    # Inicializar cliente MQTT
    client = mqtt.Client()
    client.username_pw_set(ACCESS_TOKEN)
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start()

    try:
        with serial.Serial(PORT, BAUDRATE, timeout=1) as ser, open(CSV_FILENAME, mode='w', newline='') as csv_file:
            writer = csv.writer(csv_file)
            writer.writerow(['Timestamp', 'X', 'Y', 'Z', 'Battery', 'Comms'])

            print("Esperando datos del STM32...")

            while True:
                line = ser.readline().decode('utf-8').strip()

                if not line:
                    continue

                try:
                    parts = line.split('\t')
                    if len(parts) != 5:
                        print(f"Línea no válida: {line}")
                        continue

                    x, y, z, battery, comms = parts
                    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    writer.writerow([timestamp, x, y, z, battery, comms])
                    print(f"[{timestamp}] X={x}, Y={y}, Z={z}, V={battery}, Comms={comms}")

                    # Enviar datos a ThingsBoard
                    payload = {
                        "X": float(x),
                        "Y": float(y),
                        "Z": float(z),
                        "Battery": float(battery),
                        "Comms": comms
                    }
                    client.publish(TOPIC, str(payload))

                except Exception as e:
                    print(f"Error procesando línea: {line} -> {e}")
    except KeyboardInterrupt:
        print("\nRegistro detenido por el usuario.")
    except serial.SerialException as e:
        print(f"Error al abrir el puerto serial: {e}")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == '__main__':
    main()
