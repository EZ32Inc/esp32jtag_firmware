import serial
import time

# PORT = "/dev/ttyACM0"
PORT = "/dev/tty.usbmodem101"
BAUDRATE = 115200  # Ignored by USB-JTAG, but required to open port
CHUNK_SIZE = 512
TOTAL_MB = 10
TOTAL_BYTES = TOTAL_MB * 1024 * 1024
DATA = b"x" * CHUNK_SIZE

def main():
    with serial.Serial(PORT, baudrate=BAUDRATE, timeout=1, write_timeout=2) as ser:
        print(f"Sending {TOTAL_MB} MB of data...")
        start = time.time()
        sent = 0
        while sent < TOTAL_BYTES:
            ser.write(DATA)
            sent += CHUNK_SIZE
            if sent % (1024 * 1024) == 0:
                print(f"  → Sent {sent / (1024 * 1024):.1f} MB")
        ser.flush()
        end = time.time()
        duration = end - start
        speed = TOTAL_BYTES / 1024 / duration
        print(f"Done. Sent {TOTAL_MB} MB in {duration:.2f} sec ⇒ {speed:.2f} KB/s")

if __name__ == "__main__":
    main()