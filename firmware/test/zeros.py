import serial
s = serial.Serial('/dev/ttyUSB1', 115200, timeout=1)
s.write(b'\x00' * 5)
s.close()
