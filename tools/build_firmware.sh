#!/usr/bin/env bash
# Kompilacja firmware bez Arduino IDE (Linux): avr-gcc + rdzen Arduino z pakietu + U8g2.
#   sudo apt-get install gcc-avr avr-libc binutils-avr arduino-core-avr
#   git clone --depth 1 https://github.com/olikraus/U8g2_Arduino /sciezka/u8g2
#   U8G2=/sciezka/u8g2 tools/build_firmware.sh
# Wynik: firmware/build/sterownik_oleju.hex (do wgrania avrdude lub uzycia w symulatorze).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
U8G2=${U8G2:?ustaw U8G2=sciezka do sklonowanego U8g2_Arduino}
A=${ARDUINO_AVR:-/usr/share/arduino/hardware/arduino/avr}
B=${BUILD:-$(mktemp -d)}
CORE=$A/cores/arduino; VAR=$A/variants/eightanaloginputs
WIRE=$A/libraries/Wire/src; EE=$A/libraries/EEPROM/src; SPI=$A/libraries/SPI/src; U=$U8G2/src
SK=$ROOT/firmware/sterownik_oleju
FL="-mmcu=atmega328p -DF_CPU=16000000L -DARDUINO=10806 -DARDUINO_AVR_NANO -DARDUINO_ARCH_AVR -Os -ffunction-sections -fdata-sections -flto -I$CORE -I$VAR -I$WIRE -I$WIRE/utility -I$EE -I$SPI -I$U -I$U/clib -I$SK"
CXX="avr-g++ -std=gnu++11 -fno-exceptions -fno-threadsafe-statics"
cp "$SK/sterownik_oleju.ino" "$B/sketch.cpp"
objs=(); n=0
for f in $CORE/*.c $CORE/*.cpp $CORE/*.S $WIRE/*.cpp $WIRE/utility/*.c $SPI/*.cpp $U/*.cpp $U/clib/*.c "$B/sketch.cpp"; do
  n=$((n+1)); o="$B/o$n.o"
  case $f in
    *.c) avr-gcc -std=gnu11 $FL -w -c "$f" -o "$o" ;;
    *.S) avr-gcc -x assembler-with-cpp $FL -c "$f" -o "$o" ;;
    */sketch.cpp) $CXX $FL -Wall -Wextra -c "$f" -o "$o" ;;
    *) $CXX $FL -w -c "$f" -o "$o" ;;
  esac
  objs+=("$o")
done
avr-gcc -mmcu=atmega328p -Os -flto -Wl,--gc-sections "${objs[@]}" -o "$B/fw.elf" -lm
mkdir -p "$ROOT/firmware/build"
avr-objcopy -O ihex -R .eeprom "$B/fw.elf" "$ROOT/firmware/build/sterownik_oleju.hex"
avr-size -C --mcu=atmega328p "$B/fw.elf" | grep -E "Program|Data"
