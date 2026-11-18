// Symulator sterownika pompki oleju: emulacja ATmega328P (avr8js) uruchamiajaca
// prawdziwy plik HEX firmware + modele: OLED SSD1306 (I2C), enkoder, wyjscie YETI
// przez transoptor, przekaznik/STB, silnik wycieraczki z krancowka, termopara MAX6675,
// prosty model palnika.
import {
  CPU, avrInstruction, AVRTimer, timer0Config, timer1Config, timer2Config,
  AVRIOPort, portBConfig, portCConfig, portDConfig, AVRADC, adcConfig,
  AVRTWI, twiConfig, AVREEPROM, EEPROMMemoryBackend, AVRClock, clockConfig,
  AVRWatchdog, watchdogConfig, PinState,
} from 'avr8js';

const FREQ = 16e6;
const CYCLES_PER_MS = FREQ / 1000;

function loadHex(text, target) {
  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line.startsWith(':')) continue;
    const len = parseInt(line.substr(1, 2), 16);
    const addr = parseInt(line.substr(3, 4), 16);
    const type = parseInt(line.substr(7, 2), 16);
    if (type !== 0) continue;
    for (let i = 0; i < len; i++) target[addr + i] = parseInt(line.substr(9 + i * 2, 2), 16);
  }
}

// ---------------------------------------------------------------------------
// OLED SSD1306 128x64 na I2C (adres 0x3C)
// ---------------------------------------------------------------------------
const CMD_ARGS = { 0x20: 1, 0x21: 2, 0x22: 2, 0x81: 1, 0x8d: 1, 0xa8: 1, 0xd3: 1, 0xd5: 1, 0xd9: 1, 0xda: 1, 0xdb: 1 };

class SSD1306 {
  constructor(twi) {
    this.twi = twi;
    this.fb = new Uint8Array(128 * 8);
    this.col = 0; this.page = 0; this.mode = 2;
    this.c0 = 0; this.c1 = 127; this.p0 = 0; this.p1 = 7;
    this.on = false;
    this.cmd = null; this.args = [];
    this.selected = false;
    this.dirty = true;
  }
  start() { this.twi.completeStart(); }
  stop() { this.selected = false; this.twi.completeStop(); }
  connectToSlave(addr, write) {
    this.selected = addr === 0x3c && write;
    this.expectCtrl = true;
    this.twi.completeConnect(this.selected);
  }
  writeByte(v) {
    if (!this.selected) { this.twi.completeWrite(false); return; }
    if (this.expectCtrl) {
      this.isData = (v & 0x40) !== 0;
      this.co = (v & 0x80) !== 0;
      this.expectCtrl = false;
    } else {
      if (this.isData) this.data(v); else this.command(v);
      if (this.co) this.expectCtrl = true;
    }
    this.twi.completeWrite(true);
  }
  readByte() { this.twi.completeRead(0xff); }
  command(v) {
    if (this.cmd !== null) {
      this.args.push(v);
      if (this.args.length >= CMD_ARGS[this.cmd]) { this.exec(this.cmd, this.args); this.cmd = null; }
      return;
    }
    if (CMD_ARGS[v]) { this.cmd = v; this.args = []; return; }
    if (v >= 0xb0 && v <= 0xb7) this.page = v & 7;
    else if (v <= 0x0f) this.col = (this.col & 0xf0) | v;
    else if (v >= 0x10 && v <= 0x1f) this.col = (this.col & 0x0f) | ((v & 0x0f) << 4);
    else if (v === 0xae) { this.on = false; this.dirty = true; }
    else if (v === 0xaf) { this.on = true; this.dirty = true; }
  }
  exec(c, a) {
    if (c === 0x20) this.mode = a[0] & 3;
    else if (c === 0x21) { this.c0 = a[0] & 127; this.c1 = a[1] & 127; this.col = this.c0; }
    else if (c === 0x22) { this.p0 = a[0] & 7; this.p1 = a[1] & 7; this.page = this.p0; }
  }
  data(v) {
    this.fb[this.page * 128 + (this.col & 127)] = v;
    this.dirty = true;
    if (this.mode === 2) { this.col = (this.col + 1) & 127; return; }
    this.col++;
    if (this.col > this.c1) {
      this.col = this.c0;
      this.page = this.page >= this.p1 ? this.p0 : this.page + 1;
    }
  }
  pixel(x, y) { return (this.fb[(y >> 3) * 128 + x] >> (y & 7)) & 1; }
}

// ---------------------------------------------------------------------------
// Symulacja calego ukladu
// ---------------------------------------------------------------------------
export class Simulator {
  constructor(hexText, eepromBytes) {
    const prog = new Uint16Array(0x4000);
    loadHex(hexText, new Uint8Array(prog.buffer));
    const cpu = (this.cpu = new CPU(prog));
    new AVRTimer(cpu, timer0Config);
    new AVRTimer(cpu, timer1Config);
    new AVRTimer(cpu, timer2Config);
    this.portB = new AVRIOPort(cpu, portBConfig);
    this.portC = new AVRIOPort(cpu, portCConfig);
    this.portD = new AVRIOPort(cpu, portDConfig);
    this.adc = new AVRADC(cpu, adcConfig);
    const twi = new AVRTWI(cpu, twiConfig, FREQ);
    this.oled = new SSD1306(twi);
    twi.eventHandler = this.oled;
    this.eepromBackend = new EEPROMMemoryBackend(1024);
    if (eepromBytes && eepromBytes.length === 1024) this.eepromBackend.memory.set(eepromBytes);
    new AVREEPROM(cpu, this.eepromBackend);
    const clock = new AVRClock(cpu, FREQ, clockConfig);
    new AVRWatchdog(cpu, watchdogConfig, clock);
    // Restart przez watchdoga: jak Optiboot, przekaz przyczyne resetu (WDRF) w rejestrze r2
    // i ustaw piny w stan wysokiej impedancji (jak w prawdziwym procesorze po resecie).
    this.wdtResets = 0;
    const origReset = cpu.reset.bind(cpu);
    cpu.reset = () => {
      origReset();
      cpu.data[2] = 0x08;
      for (const a of [0x24, 0x25, 0x27, 0x28, 0x2a, 0x2b]) cpu.data[a] = 0;  // DDRx/PORTx
      cpu.data[0x80] = 0; cpu.data[0x81] = 0;                               // TCCR1A/B
      this.wdtResets++;
    };

    // ---- stan "swiata"
    this.timeMs = 0;
    this.blower = false;      // YETI zalacza wyjscie "Dmuchawa"
    this.stbOk = true;        // STB / bezpiecznik 12 V
    this.relayWelded = false; // sklejony styk przekaznika
    this.encQueue = [];
    this.button = false;

    this.mlPerRev = 10;       // wydajnosc pompy [ml/obrot walka]
    this.rpmMax = 50;         // obroty walka przy 100 % [obr/min]
    this.jammed = false;
    this.angle = 90;          // kat walka [stopnie], 0..PARK = pozycja parkowania
    this.revs = 0;
    this.oilMl = 0;
    this.rpm = 0;
    this.duty = 0;
    this.flowBins = new Float64Array(30); // ml na sekunde, ostatnie 30 s
    this.lastOilMs = -1e9;

    this.flameLit = false;
    this.autoIgnite = true;
    this.ignitePendingMs = 0;
    this.temp = 20;
    this.tcOpen = false;

    // ---- MAX6675 na programowym SPI: CS = B2 (D10), SO = B4 (D12), SCK = B5 (D13)
    this.tcWord = 0; this.tcBit = -1;
    this.portB.addListener((v, old) => {
      const cs = (v >> 2) & 1, sck = (v >> 5) & 1, ocs = (old >> 2) & 1, osck = (old >> 5) & 1;
      if (ocs && !cs) {
        this.tcWord = this.tcOpen ? 0x0004 : (Math.max(0, Math.round(this.temp * 4)) & 0xfff) << 3;
        this.tcBit = 15;
        this.portB.setPin(4, !!((this.tcWord >> 15) & 1));
      } else if (!cs && sck && !osck && this.tcBit >= 0) {
        this.tcBit--;
        if (this.tcBit >= 0) this.portB.setPin(4, !!((this.tcWord >> this.tcBit) & 1));
      }
    });

    // spoczynkowe stany wejsc (podciagniete do + w MCU)
    this.portD.setPin(2, true); this.portD.setPin(3, true); this.portD.setPin(4, true);
    this.portD.setPin(5, true); this.portD.setPin(6, true); this.portB.setPin(0, true);
    this.encAB = 3;
  }

  // ---- enkoder: jeden zabek = 4 zmiany stanu A/B (po 1 ms emulacji kazda)
  rotate(steps) {
    const cw = [1, 0, 2, 3];   // AB: 11 -> 01 -> 00 -> 10 -> 11  (+1 zabek)
    const ccw = [2, 0, 1, 3];
    const seq = steps > 0 ? cw : ccw;
    for (let i = 0; i < Math.abs(steps); i++) for (const s of seq) this.encQueue.push(s);
  }
  setButton(down) { this.button = down; }

  get v12() { return (this.blower && this.stbOk) || this.relayWelded; }
  get buzzer() { return this.portD.pinState(7) === PinState.High; }
  get flowLph() {
    let s = 0;
    for (const b of this.flowBins) s += b;
    return (s / 30) * 3600 / 1000;
  }
  ignite() { this.autoIgnite = true; this.flameLit = true; }
  extinguish() { this.autoIgnite = false; this.flameLit = false; }

  readDuty() {
    const d = this.cpu.data;
    const tccr1a = d[0x80];
    const icr = d[0x86] | (d[0x87] << 8);
    const ocr = d[0x88] | (d[0x89] << 8);
    if ((tccr1a & 0x80) && icr > 0) return Math.min(1, ocr / icr);
    return this.portB.pinState(1) === PinState.High ? 1 : 0;
  }

  // jedna milisekunda: wejscia -> CPU -> modele
  stepMs() {
    const t = this.timeMs;
    // YETI przez transoptor: przy 230 V wyjscie modulu LOW przez ~70 % kazdej polowki sinusoidy
    this.portD.setPin(5, !(this.blower && (t % 10) < 7));
    if (this.encQueue.length) {
      const s = this.encQueue.shift();
      this.portD.setPin(2, !!(s & 2));
      this.portD.setPin(3, !!(s & 1));
    }
    this.portD.setPin(4, !this.button);
    this.portD.setPin(6, !(this.temp > 60));                 // termostat spalin NO: zwarty = goraco
    this.portB.setPin(0, !((this.angle % 360) < 20));          // krancowka: LOW w pozycji parkowania
    this.adc.channelValues[0] = this.v12 ? 12.6 * 3.3 / 13.3 : 0.02;

    const cpu = this.cpu;
    const end = cpu.cycles + CYCLES_PER_MS;
    while (cpu.cycles < end) {
      avrInstruction(cpu);
      cpu.tick();
    }

    // silnik + pompa
    this.duty = this.v12 ? this.readDuty() : 0;
    const target = this.jammed || this.duty < 0.15 ? 0 : this.rpmMax * this.duty;
    this.rpm += (target - this.rpm) * 0.05;                   // bezwladnosc ~20 ms
    if (target === 0 && this.rpm < 1) this.rpm = 0;
    const dRev = this.rpm / 60000;
    this.angle = (this.angle + dRev * 360) % 360;
    this.revs += dRev;
    const ml = dRev * this.mlPerRev;
    this.oilMl += ml;
    this.flowBins[Math.floor(t / 1000) % 30] += ml;
    if ((t + 1) % 1000 === 0) this.flowBins[Math.floor((t + 1) / 1000) % 30] = 0;
    if (ml > 0) this.lastOilMs = t;

    // palnik: plomien podtrzymywany olejem, temperatura dazy do celu (stala czasowa 8 s)
    const sinceOil = t - this.lastOilMs;
    if (this.flameLit && sinceOil > 45000) this.flameLit = false;  // wypalil sie
    if (!this.flameLit && this.autoIgnite && sinceOil < 10000) {
      this.ignitePendingMs += 1;
      if (this.ignitePendingMs > 3000) { this.flameLit = true; this.ignitePendingMs = 0; }
    } else this.ignitePendingMs = 0;
    const tgt = this.flameLit ? 70 + 230 * Math.min(1, this.flowLph / 2) : 20;
    this.temp += (tgt - this.temp) * (1 / 8000);

    this.timeMs++;
  }

  run(ms) { for (let i = 0; i < ms; i++) this.stepMs(); }
}
