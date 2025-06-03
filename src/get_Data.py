import serial
import csv
from datetime import datetime

# Configuración del puerto serial
PORT = '/dev/ttyACM0'
BAUDRATE = 115200
CSV_FILENAME = 'logs.csv'

def main():
    try:
        # Abrir puerto serial
        with serial.Serial(PORT, BAUDRATE, timeout=1) as ser, open(CSV_FILENAME, mode='w', newline='') as csv_file:
            writer = csv.writer(csv_file)
            # Escribir encabezados
            writer.writerow(['Timestamp', 'X', 'Y', 'Z', 'Battery', 'Comms'])

            print("Esperando datos del STM32... (Ctrl+C para detener)")
            
            while True:
                line = ser.readline().decode('utf-8').strip()

                if not line:
                    continue  # ignorar líneas vacías

                try:
                    parts = line.split('\t')
                    if len(parts) != 5:
                        print(f"Línea no válida: {line}")
                        continue

                    x, y, z, battery, comms = parts
                    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    writer.writerow([timestamp, x, y, z, battery, comms])
                    print(f"[{timestamp}] X={x}, Y={y}, Z={z}, V={battery}, Comms={comms}")

                except Exception as e:
                    print(f"Error procesando línea: {line} -> {e}")
    except KeyboardInterrupt:
        print("\nRegistro detenido por el usuario.")
    except serial.SerialException as e:
        print(f"Error al abrir el puerto serial: {e}")

if __name__ == '__main__':
    main()
