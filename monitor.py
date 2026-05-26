import serial
import sys
import time

ser = serial.Serial('COM3', baudrate=115200, timeout=0.1)
ser.reset_input_buffer()
ser.reset_output_buffer()

print("=== Serial Monitor COM3 @ 115200 ===", flush=True)

while True:
    try:
        line = ser.readline().decode('utf-8', errors='replace').rstrip()
        if line:
            print(line, flush=True)
    except KeyboardInterrupt:
        break
    except Exception as e:
        print(f'Error: {e}', file=sys.stderr, flush=True)
        time.sleep(1)
