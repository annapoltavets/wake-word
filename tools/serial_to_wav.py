#!/usr/bin/env python3
"""Збирає записи мікрофона з serial-виводу прошивки (REC_BEGIN / R <hex> / REC_END) у WAV.

Використання (закрийте pio monitor, порт має бути вільним):
    python tools/serial_to_wav.py /dev/cu.usbserial-0001 [-o recordings]
    python tools/serial_to_wav.py --file saved_log.txt
"""
import argparse
import datetime
import os
import struct
import sys
import wave


def lines_from_serial(port, baud):
    import serial  # pyserial (є в ~/.platformio/penv)

    with serial.Serial(port, baud, timeout=1) as ser:
        while True:
            raw = ser.readline()
            if raw:
                yield raw.decode("utf-8", errors="replace").rstrip("\r\n")


def lines_from_file(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            yield line.rstrip("\r\n")


def save_wav(out_dir, samples, rate):
    os.makedirs(out_dir, exist_ok=True)
    name = datetime.datetime.now().strftime("rec_%Y%m%d_%H%M%S.wav")
    path = os.path.join(out_dir, name)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?")
    ap.add_argument("--file")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("-o", "--out", default="recordings")
    args = ap.parse_args()
    if not args.port and not args.file:
        ap.error("вкажіть порт або --file")

    lines = lines_from_file(args.file) if args.file else lines_from_serial(args.port, args.baud)

    rate = 16000
    expected = 0
    samples = None
    for line in lines:
        if line.startswith("REC_BEGIN"):
            _, n, r = line.split()
            expected, rate = int(n), int(r)
            samples = []
            print("запис почався, %d семплів..." % expected)
        elif line.startswith("R ") and samples is not None:
            h = line[2:].strip()
            for i in range(0, len(h) - 3, 4):
                samples.append(struct.unpack("<h", struct.pack("<H", int(h[i:i + 4], 16)))[0])
        elif line.startswith("REC_END") and samples is not None:
            want = int(line.split()[1])
            got = sum(v & 0xFFFF for v in samples) & 0xFFFFFFFF
            if len(samples) != expected or got != want:
                print("пошкоджений запис (семплів %d/%d, сума %d/%d), пропущено"
                      % (len(samples), expected, got, want))
            else:
                peak = max(abs(v) for v in samples)
                print("збережено %s (peak %d)" % (save_wav(args.out, samples, rate), peak))
            samples = None
        else:
            print(line)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
