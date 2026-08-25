/* =====================================================================
 *  SensorLogger.ino
 *  ---------------------------------------------------------------------
 *  Reads six sensor drivers once per second and writes one row per
 *  second into a text file. Every 30 rows the file is closed and a new
 *  one opened.
 *
 *  File naming: 639_SSSSSS  where SSSSSS is the uptime in seconds at the
 *  moment the file is CLOSED, zero-padded to six digits.
 *
 *      639_000030   rows for seconds  0..29
 *      639_000060   rows for seconds 30..59
 *      639_000090   rows for seconds 60..89
 *      ...
 *
 *  Runs for 3 minutes: 180 rows across 6 files, then stops.
 *
 *  No CSP, no KISS, no framing - just sensors to file.
 * ===================================================================== */

#include <Arduino.h>

#include "src/SensorTypes.h"
#include "src/DummySource.h"
#include "src/Tmp117Driver.h"
#include "src/Bpx65Driver.h"
#include "src/Bpw34Driver.h"
#include "src/N3163Driver.h"
#include "src/Adr4525Driver.h"
#include "src/MemoryScanDriver.h"

/* =====================================================================
 *  CONFIGURATION
 * ===================================================================== */

#define SAMPLE_PERIOD_MS   1000UL   /* 1 Hz                             */

/* ---------------------------------------------------------------------
 *  Two INDEPENDENT periods.
 *
 *  TX_PERIOD_S    how often a frame goes to the OBC. This is the
 *                 heartbeat, sent from a RAM buffer.
 *  FILE_PERIOD_S  how often the disk file rolls over. Bigger files waste
 *                 far less flash: LittleFS allocates whole 4096 B
 *                 blocks, so a 226 kB hourly file has 1.3% slack against
 *                 51% for a 2 kB file.
 *
 *  They can differ because the transmit path never reads the file back -
 *  each row is appended to both as it is produced.
 * ------------------------------------------------------------------- */
#define TX_PERIOD_S        30       /* heartbeat to the OBC             */
#define FILE_PERIOD_S      3600     /* disk file rollover, 1 hour       */
#define RUN_DURATION_S     7200     /* 2 hours. 0 = run forever         */

#define ROWS_PER_FRAME     TX_PERIOD_S
#define ROWS_PER_FILE      FILE_PERIOD_S

/* ---------------------------------------------------------------------
 *  OBC link
 *
 *  The whole file is pushed to the OBC over UART the moment it closes.
 *  Framing is plain text so it can be read with any serial terminal:
 *
 *      <FILE 639_000030>
 *      sec,die_temp_C,...              <- header line
 *      0,-5,94,...                     <- 30 rows
 *      ...
 *      <END 639_000030 2143 8A41C0D2>  <- name, byte count, CRC32
 *
 *  Byte count and CRC sit in the END line rather than the FILE line so
 *  the payload can stream the rows out in one pass instead of scanning
 *  the file twice.
 *
 *  If USE_ACK is on the payload waits for "ACK <name>" and retries on
 *  timeout or "NAK". The file stays on flash either way, so a failed
 *  transfer is recoverable later.
 * ------------------------------------------------------------------- */
#define OBC_UART           Serial1
#define OBC_BAUD           115200

#if defined(ESP32)
  #define OBC_RX_PIN       16
  #define OBC_TX_PIN       17
#endif

#define SEND_TO_OBC        1

/* The OBC does not reply. It treats the arrival of a frame every 30 s as
 * the payload's heartbeat, so the transmission itself is the liveness
 * signal - which changes two things:
 *
 *   1. A frame MUST go out every window, even a degraded one. Staying
 *      silent because something went wrong would read as "payload dead"
 *      rather than "payload had a bad file". Better to send a short
 *      frame the OBC can flag than nothing at all.
 *   2. The CRC becomes advisory. The OBC can detect a corrupt frame but
 *      cannot ask for it again, so a bad frame is simply dropped on
 *      their side. The file stays on flash for later retrieval.
 */
#define USE_ACK            0
#define ACK_TIMEOUT_MS     2000UL
#define MAX_SEND_ATTEMPTS  3

/* One file must fit here: 30 rows x ~115 B + header. */
#define TX_BUFFER_BYTES    4096

#define FILE_PREFIX        "639_"
#define FILE_EXT           ""       /* set to ".txt" if you want one    */

#define WRITE_HEADER       1        /* column names as the first line   */

/* ---------------------------------------------------------------------
 *  Housekeeping of stored files
 *
 *  CLEAN_ON_BOOT   delete every 639_* file at startup, so each run
 *                  begins with an empty volume. Filenames restart from
 *                  639_000030 every run, so without this the second run
 *                  collides with the first.
 *
 *  RING_BUFFER     when free space drops below MIN_FREE_BYTES, delete
 *                  the OLDEST file (lowest number in the name) and keep
 *                  going. Without it the volume simply fills and writes
 *                  begin failing - silently, because println() does not
 *                  report errors.
 * ------------------------------------------------------------------- */
#define CLEAN_ON_BOOT      1

/* MAX_STORED_FILES 0 = size the ring automatically from the partition,
 * leaving RESERVE_PERCENT free for metadata and wear levelling.       */
#define MAX_STORED_FILES   0
#define RESERVE_PERCENT    20

#define ACCEPT_COMMANDS    1
#define CMD_LINE_MAX       48

/* ---------------------------------------------------------------------
 *  Output format
 *
 *  ENGINEERING (default) writes degrees C and volts.
 *  RAW writes the device codes exactly as read.
 *
 *  Note the trade: converting here bakes the calibration constants into
 *  the file. If a gain or reference value is later found to be wrong,
 *  raw codes can be reprocessed and engineering values cannot. Keep RAW
 *  if the archive needs to survive a calibration change.
 * ------------------------------------------------------------------- */
#define OUTPUT_ENGINEERING 1

/* Conversion constants - must match the hardware configuration. */
#define ADS_FSR_VOLTS      4.096f   /* ADS1115 PGA = GAIN_ONE           */
#define ADS_FULL_SCALE     32768.0f
#define TMP117_LSB_C       (1.0f / 128.0f)
/* ---------------------------------------------------------------------
 *  Serial monitor display
 *
 *    DISPLAY_OFF    only [OPEN]/[SEND]/[PRUNE] events
 *    DISPLAY_TABLE  aligned columns, one line per sample (default)
 *    DISPLAY_CSV    the exact row as written to the file
 *
 *  The table repeats its header every HEADER_EVERY rows so the columns
 *  stay labelled as the window scrolls.
 * ------------------------------------------------------------------- */
#define DISPLAY_OFF        0
#define DISPLAY_TABLE      1
#define DISPLAY_CSV        2

#define DISPLAY_MODE       DISPLAY_TABLE
#define HEADER_EVERY       20

/* ---------------------------------------------------------------------
 *  Storage backend
 *
 *  LITTLEFS  internal flash partition. No extra hardware.
 *  SD        SD card module on SPI.
 *  SERIAL    no filesystem at all - the "files" are printed to the
 *            serial monitor with BEGIN/END markers. Useful on a board
 *            with no storage, or to check the format before committing
 *            to a filesystem.
 * ------------------------------------------------------------------- */
#define STORAGE_LITTLEFS   0
#define STORAGE_SD         1
#define STORAGE_SERIAL     2

#define STORAGE_BACKEND    STORAGE_LITTLEFS

#if STORAGE_BACKEND == STORAGE_LITTLEFS
  #include <LittleFS.h>
  #define FS_HANDLE LittleFS
#elif STORAGE_BACKEND == STORAGE_SD
  #include <SD.h>
  #include <SPI.h>
  #define SD_CS_PIN 5
#endif

/* =====================================================================
 *  STATE
 * ===================================================================== */

static uint32_t uptimeS      = 0;    /* seconds since start            */
static uint32_t rowsInFile   = 0;
static uint16_t filesWritten = 0;
static bool     runComplete  = false;
static uint32_t lastSampleMs = 0;

#if STORAGE_BACKEND != STORAGE_SERIAL
static File     logFile;
#endif
static char     currentName[24];

/* The outgoing copy is accumulated in RAM as rows are written, so the
 * file never has to be read back to transmit it. Works the same in all
 * three storage backends.                                             */
static char     txBuf[TX_BUFFER_BYTES];
static uint16_t txLen = 0;
static bool     txOverflow = false;

static uint32_t framesSent  = 0;
static uint32_t prunedFiles = 0;
static uint16_t maxFiles    = 0;
static uint16_t rowsInFrame = 0;

/* CRC32, standard reflected polynomial 0xEDB88320. Bitwise to keep the
 * flash cost near zero; at 115200 baud the link is the bottleneck.    */
static uint32_t crc32_init() { return 0xFFFFFFFFUL; }

static uint32_t crc32_update(uint32_t crc, const uint8_t *d, uint16_t n)
{
  for (uint16_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (uint8_t k = 0; k < 8; k++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
  }
  return crc;
}

static uint32_t crc32_final(uint32_t crc) { return ~crc; }

static uint32_t crc32(const uint8_t *d, uint16_t n)
{
  return crc32_final(crc32_update(crc32_init(), d, n));
}

static void txReset(void)
{
  txLen = 0;
  txOverflow = false;
}

static void txAppend(const char *s)
{
  uint16_t n = (uint16_t)strlen(s);
  if (txLen + n + 1 >= TX_BUFFER_BYTES) { txOverflow = true; return; }
  memcpy(txBuf + txLen, s, n);
  txLen += n;
  txBuf[txLen++] = '\n';
}

/* =====================================================================
 *  FILE NAMING
 *
 *  The number in the name is the uptime at CLOSE, i.e. the end of the
 *  window the file covers. A file opened at second 0 is named for
 *  second 30.
 * ===================================================================== */
static void buildFileName(char *dst, size_t n, uint32_t closeSecond)
{
  snprintf(dst, n, "/%s%06lu%s",
           FILE_PREFIX, (unsigned long)closeSecond, FILE_EXT);
}

#if OUTPUT_ENGINEERING
static const char *COLUMN_HEADER =
  "sec,"
  "tmp0_C,tmp1_C,"
  "bpx65_a_V,bpx65_b_V,bpw34_a_V,bpw34_b_V,"
  "n3163_a,n3163_b,adr4525_V,"
  "psram,fram,eeprom";
#else
static const char *COLUMN_HEADER =
  "sec,"
  "tmp0,tmp1,"
  "bpx65_a,bpx65_b,bpw34_a,bpw34_b,"
  "n3163_a,n3163_b,adr4525,"
  "psram,fram,eeprom";
#endif

/* Raw code -> engineering unit. Kept as functions rather than inline
 * arithmetic so there is exactly one place to change if the PGA range
 * or the reference is reconfigured.                                   */
static inline float tmp117_toC(int16_t raw)  { return raw * TMP117_LSB_C; }
static inline float ads_toVolts(int16_t raw) { return raw * ADS_FSR_VOLTS / ADS_FULL_SCALE; }

/* =====================================================================
 *  STORED FILE HOUSEKEEPING
 * ===================================================================== */
#if STORAGE_BACKEND != STORAGE_SERIAL

/* Is this one of our data files? Guards against deleting anything else
 * that happens to live on the volume.                                  */
static bool isDataFile(const char *name)
{
  const char *n = (name[0] == '/') ? name + 1 : name;
  return strncmp(n, FILE_PREFIX, strlen(FILE_PREFIX)) == 0;
}

/* Numeric part of the name, used to find the oldest file. */
static uint32_t fileNumber(const char *name)
{
  const char *n = (name[0] == '/') ? name + 1 : name;
  return (uint32_t)strtoul(n + strlen(FILE_PREFIX), nullptr, 10);
}

static uint16_t deleteAllDataFiles(void)
{
  uint16_t removed = 0;
  char victim[24];

  /* Delete one per pass and re-open the directory each time. Removing
   * entries while iterating is not safe on every filesystem.          */
  for (;;) {
    victim[0] = 0;
    File root = FS_HANDLE.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      if (isDataFile(f.name())) {
        const char *n = f.name();
        snprintf(victim, sizeof(victim), "%s%s", (n[0] == '/') ? "" : "/", n);
        f.close();
        break;
      }
      f.close();
    }
    root.close();
    if (!victim[0]) break;
    if (!FS_HANDLE.remove(victim)) break;
    removed++;
  }
  return removed;
}

static uint16_t countDataFiles(void)
{
  uint16_t n = 0;
  File root = FS_HANDLE.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (isDataFile(f.name())) n++;
    f.close();
  }
  root.close();
  return n;
}

static bool deleteOldestDataFile(void)
{
  char oldest[24] = {0};
  uint32_t oldestNum = 0xFFFFFFFFUL;

  File root = FS_HANDLE.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (isDataFile(f.name())) {
      uint32_t num = fileNumber(f.name());
      if (num < oldestNum) {
        oldestNum = num;
        const char *n = f.name();
        snprintf(oldest, sizeof(oldest), "%s%s", (n[0] == '/') ? "" : "/", n);
      }
    }
    f.close();
  }
  root.close();

  if (!oldest[0]) return false;
  if (!FS_HANDLE.remove(oldest)) return false;

  prunedFiles++;
  Serial.print(F("[PRUNE] "));
  Serial.println(oldest + 1);
  return true;
}

/* Bytes one full file occupies, rounded up to whole 4 kB blocks. Used
 * only to size the ring, so an approximation is fine.                */
static uint32_t estimatedFileBytes(void)
{
  const uint32_t bytesPerRow = 64;               /* measured ~63 B */
  uint32_t content = strlen(COLUMN_HEADER) + 1
                   + (uint32_t)ROWS_PER_FILE * bytesPerRow;
  return ((content + 4095UL) / 4096UL) * 4096UL;
}

static uint16_t resolveMaxFiles(void)
{
#if MAX_STORED_FILES > 0
  return MAX_STORED_FILES;
#elif STORAGE_BACKEND == STORAGE_LITTLEFS
  uint32_t usable = (uint32_t)LittleFS.totalBytes() * (100 - RESERVE_PERCENT) / 100;
  uint32_t per = estimatedFileBytes();
  uint32_t n = per ? usable / per : 2;
  if (n < 2)    n = 2;                 /* always keep the previous file */
  if (n > 1000) n = 1000;
  return (uint16_t)n;
#else
  return 24;                           /* SD: no free-space API */
#endif
}

/* Prune oldest-first until the ring is within budget. Bounded so a
 * filesystem that cannot free space does not spin here forever.      */
static void ensureSpace(void)
{
  for (uint8_t guard = 0; guard < 32; guard++) {
    if (countDataFiles() < maxFiles) return;
    if (!deleteOldestDataFile()) {
      Serial.println(F("[ERR]   nothing left to prune"));
      return;
    }
  }
}
#endif /* not STORAGE_SERIAL */

/* =====================================================================
 *  OPEN / CLOSE
 * ===================================================================== */
static bool openNextFile(void)
{
  /* Name it for the second at which it will be closed. */
  uint32_t closeSecond = uptimeS + ROWS_PER_FILE;
  buildFileName(currentName, sizeof(currentName), closeSecond);

#if STORAGE_BACKEND == STORAGE_SERIAL
  Serial.print(F("\n===== BEGIN "));
  Serial.print(currentName + 1);
  Serial.println(F(" ====="));
  #if WRITE_HEADER
  Serial.println(COLUMN_HEADER);
  #endif
#else
    ensureSpace();

  /* Open truncating, not appending. FILE_WRITE means "w" (truncate) on
   * the ESP32 FS layer but O_APPEND in the classic SD library - so a
   * re-run would quietly double the rows in a file. Remove first to get
   * the same behaviour on every backend.                              */
  if (FS_HANDLE.exists(currentName)) FS_HANDLE.remove(currentName);

  logFile = FS_HANDLE.open(currentName, FILE_WRITE);
  if (!logFile) {
    Serial.print(F("[ERR] cannot open "));
    Serial.println(currentName);
    return false;
  }
  #if WRITE_HEADER
  logFile.println(COLUMN_HEADER);
  #endif
  Serial.print(F("[OPEN]  "));
  Serial.println(currentName + 1);
#endif

  txReset();
#if WRITE_HEADER
  txAppend(COLUMN_HEADER);
#endif

  rowsInFile = 0;
  return true;
}

static void closeCurrentFile(void)
{
#if STORAGE_BACKEND == STORAGE_SERIAL
  Serial.print(F("===== END "));
  Serial.print(currentName + 1);
  Serial.print(F("  ("));
  Serial.print(rowsInFile);
  Serial.println(F(" rows) ====="));
#else
  if (logFile) {
    logFile.close();
    Serial.print(F("[CLOSE] "));
    Serial.print(currentName + 1);
    Serial.print(F("  "));
    Serial.print(rowsInFile);
    Serial.println(F(" rows"));
  }
#endif
  filesWritten++;
}

/* =====================================================================
 *  SERIAL DISPLAY
 * ===================================================================== */
#if DISPLAY_MODE == DISPLAY_TABLE
static uint16_t displayRow = 0;

static void printTableHeader(void)
{
  Serial.println();
  Serial.println(F("    sec |   TMP0   TMP1 |   BPX65a   BPX65b   BPW34a   BPW34b | 3N |   ADR | PS FR EE"));
  Serial.println(F("--------+---------------+-------------------------------------+----+-------+---------"));
}

static void printTableRow(const Tmp117Data &tp, const Bpx65Data &bx,
                          const Bpw34Data &bw, const N3163Data &ev,
                          const Adr4525Data &ad, const MemoryScanData &ms)
{
  if ((displayRow % HEADER_EVERY) == 0) printTableHeader();
  displayRow++;

  char line[128];
  snprintf(line, sizeof(line),
    "%7lu | %6.1f %6.1f | %8.4f %8.4f %8.4f %8.4f | %u%u | %5.2f | %2u %2u %2u",
    (unsigned long)uptimeS,
    tmp117_toC(tp.raw[0]), tmp117_toC(tp.raw[1]),
    ads_toVolts(bx.raw[0]), ads_toVolts(bx.raw[1]),
    ads_toVolts(bw.raw[0]), ads_toVolts(bw.raw[1]),
    ev.bit[0], ev.bit[1], ads_toVolts(ad.raw),
    ms.psram, ms.fram, ms.eeprom);
  Serial.println(line);
}
#endif

/* =====================================================================
 *  ONE ROW
 * ===================================================================== */
static void writeRow(void)
{
  Tmp117Data     tp;
  Bpx65Data      bx;
  Bpw34Data      bw;
  N3163Data      ev;
  Adr4525Data    ad;
  MemoryScanData ms;

  /* Seven independent driver reads, exactly as a real payload would. */
  tmp117_read(tp);
  bpx65_read(bx);
  bpw34_read(bw);
  n3163_read(ev);
  adr4525_read(ad);
  memscan_read(ms);

  char row[224];

#if OUTPUT_ENGINEERING
  /* TMP117 LSB is 1/128 C = 0.0078125, so 4 decimals is exact.
   * ADS1115 LSB at +-4.096 V is 125 uV, so 6 decimals is exact.
   * Note: float formatting in printf needs no special flags on ESP32,
   * but on AVR it requires linking the full vfprintf.                 */
  snprintf(row, sizeof(row),
    "%lu,"
    "%.1f,%.1f,"
    "%.4f,%.4f,%.4f,%.4f,"
    "%u,%u,%.2f,"
    "%u,%u,%u",
    (unsigned long)uptimeS,
    tmp117_toC(tp.raw[0]), tmp117_toC(tp.raw[1]),
    ads_toVolts(bx.raw[0]), ads_toVolts(bx.raw[1]),
    ads_toVolts(bw.raw[0]), ads_toVolts(bw.raw[1]),
    ev.bit[0], ev.bit[1], ads_toVolts(ad.raw),
    ms.psram, ms.fram, ms.eeprom);
#else
  snprintf(row, sizeof(row),
    "%lu,"
    "%d,%d,"
    "%d,%d,%d,%d,"
    "%u,%u,%d,"
    "%u,%u,%u",
    (unsigned long)uptimeS,
    tp.raw[0], tp.raw[1],
    bx.raw[0], bx.raw[1], bw.raw[0], bw.raw[1],
    ev.bit[0], ev.bit[1], ad.raw,
    ms.psram, ms.fram, ms.eeprom);
#endif

#if STORAGE_BACKEND == STORAGE_SERIAL
  Serial.println(row);
#else
  logFile.println(row);
  /* Flush every row. Holding 30 s of data in a buffer means a power cut
   * loses the whole file; flushing costs a little wear but bounds the
   * loss to one row.                                                   */
  logFile.flush();
#endif

#if DISPLAY_MODE == DISPLAY_TABLE
  printTableRow(tp, bx, bw, ev, ad, ms);
#elif DISPLAY_MODE == DISPLAY_CSV
  Serial.println(row);
#endif

  txAppend(row);
  rowsInFile++;
  rowsInFrame++;
}

/* =====================================================================
 *  TRANSMIT TO OBC
 * ===================================================================== */
#if SEND_TO_OBC

static void sendFrame(const char *name, const char *body, uint16_t len)
{
  char line[48];
  snprintf(line, sizeof(line), "<FILE %s>", name);
  OBC_UART.println(line);

  OBC_UART.write((const uint8_t *)body, len);

  uint32_t crc = crc32((const uint8_t *)body, len);
  snprintf(line, sizeof(line), "<END %s %u %08lX>",
           name, (unsigned)len, (unsigned long)crc);
  OBC_UART.println(line);
  OBC_UART.flush();
}

/* Heartbeat, named for the second at which this 30 s window closes.
 * Consecutive names step by exactly TX_PERIOD_S, so the OBC spots a
 * missed beat from the name alone.                                   */
static void sendHeartbeat(void)
{
  char name[24];
  snprintf(name, sizeof(name), "%s%06lu", FILE_PREFIX, (unsigned long)uptimeS);

  if (txOverflow) {
    /* Send the short frame anyway: silence would read as a dead payload
     * rather than one bad window.                                     */
    Serial.print(F("[WARN]  tx overflow, sending "));
    Serial.print(txLen);
    Serial.println(F(" B"));
  }

  sendFrame(name, txBuf, txLen);
  framesSent++;

  Serial.print(F("[SEND]  "));
  Serial.print(name);
  Serial.print(F("  "));
  Serial.print(txLen);
  Serial.println(F(" B"));

  txReset();
#if WRITE_HEADER
  txAppend(COLUMN_HEADER);            /* keep every frame self-describing */
#endif
  rowsInFrame = 0;
}
#endif /* SEND_TO_OBC */


/* =====================================================================
 *  OBC COMMANDS
 *
 *    LIST            names and sizes of stored files
 *    STATUS          uptime, counters, free space, ring size
 *    RESEND <name>   stream a stored file back
 *
 *  RESEND is what makes the flash copy worth keeping: without it the
 *  stored files are write-only and a missed or corrupt heartbeat is
 *  unrecoverable.
 *
 *  Commands run between sample ticks, so even a long RESEND cannot
 *  delay a sample by more than one sample period.
 * ===================================================================== */
#if ACCEPT_COMMANDS && SEND_TO_OBC

static char    cmdBuf[CMD_LINE_MAX];
static uint8_t cmdLen = 0;

static void cmdList(void)
{
#if STORAGE_BACKEND != STORAGE_SERIAL
  OBC_UART.println("<LIST>");
  File root = FS_HANDLE.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (isDataFile(f.name())) {
      const char *n = f.name();
      OBC_UART.print((n[0] == '/') ? n + 1 : n);
      OBC_UART.print(' ');
      OBC_UART.println((unsigned long)f.size());
    }
    f.close();
  }
  root.close();
  OBC_UART.println("<ENDLIST>");
#else
  OBC_UART.println("<ERR no filesystem>");
#endif
}

static void cmdStatus(void)
{
  char line[144];
#if STORAGE_BACKEND == STORAGE_LITTLEFS
  unsigned long freeB = (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes());
#else
  unsigned long freeB = 0;
#endif
  snprintf(line, sizeof(line),
    "<STATUS uptime=%lu files=%lu frames=%lu pruned=%lu free=%lu ring=%u>",
    (unsigned long)uptimeS, (unsigned long)filesWritten,
    (unsigned long)framesSent, (unsigned long)prunedFiles,
    freeB, maxFiles);
  OBC_UART.println(line);
}

/* Stream a stored file back in the same framing as a heartbeat.
 * The CRC accumulates as the body streams, so the file is read once and
 * never held whole in RAM - an hourly file is ~230 kB.                */
static void cmdResend(const char *name)
{
#if STORAGE_BACKEND == STORAGE_SERIAL
  OBC_UART.println("<ERR no filesystem>");
#else
  char path[24];
  snprintf(path, sizeof(path), "%s%s", (name[0] == '/') ? "" : "/", name);

  if (!isDataFile(name) || !FS_HANDLE.exists(path)) {
    OBC_UART.print("<ERR not found ");
    OBC_UART.print(name); OBC_UART.println(">");
    return;
  }

  /* Refuse the file currently being written: its length changes
   * mid-stream and the CRC could never match.                        */
  if (strcmp(path, currentName) == 0) {
    OBC_UART.print("<ERR file open ");
    OBC_UART.print(name); OBC_UART.println(">");
    return;
  }

  File f = FS_HANDLE.open(path, FILE_READ);
  if (!f) {
    OBC_UART.print("<ERR cannot open ");
    OBC_UART.print(name); OBC_UART.println(">");
    return;
  }

  char line[48];
  snprintf(line, sizeof(line), "<FILE %s>", name);
  OBC_UART.println(line);

  uint32_t crc = crc32_init();
  uint32_t total = 0;
  uint8_t chunk[128];
  int n;
  while ((n = f.read(chunk, sizeof(chunk))) > 0) {
    OBC_UART.write(chunk, (size_t)n);
    crc = crc32_update(crc, chunk, (uint16_t)n);
    total += (uint32_t)n;
  }
  f.close();

  snprintf(line, sizeof(line), "<END %s %lu %08lX>",
           name, (unsigned long)total, (unsigned long)crc32_final(crc));
  OBC_UART.println(line);
  OBC_UART.flush();

  Serial.print(F("[RESEND] "));
  Serial.print(name); Serial.print(F("  "));
  Serial.print(total); Serial.println(F(" B"));
#endif
}

static void executeCommand(char *line)
{
  while (*line == ' ') line++;
  if (!*line) return;

  Serial.print(F("[CMD]   "));
  Serial.println(line);

  if (strncmp(line, "LIST", 4)    == 0) { cmdList();            return; }
  if (strncmp(line, "STATUS", 6)  == 0) { cmdStatus();          return; }
  if (strncmp(line, "RESEND ", 7) == 0) { cmdResend(line + 7);  return; }

  OBC_UART.print("<ERR unknown ");
  OBC_UART.print(line);
  OBC_UART.println(">");
}

static void pollCommands(void)
{
  while (OBC_UART.available()) {
    char c = (char)OBC_UART.read();
    if (c == '\n' || c == '\r') {
      if (cmdLen) { cmdBuf[cmdLen] = 0; executeCommand(cmdBuf); cmdLen = 0; }
      continue;
    }
    if (cmdLen < CMD_LINE_MAX - 1) cmdBuf[cmdLen++] = c;
    else cmdLen = 0;                        /* overlong line: discard */
  }
}
#endif /* ACCEPT_COMMANDS */

/* =====================================================================
 *  SETUP / LOOP
 * ===================================================================== */
/* Printed straight after the mount so the storage lines stay together.
 * "free" already accounts for LittleFS metadata and wear-levelling
 * reserve, so it is the number to compare against the run size.      */
#if STORAGE_BACKEND != STORAGE_SERIAL
static void PRINT_VOLUME_INFO(void)
{
  maxFiles = resolveMaxFiles();

  #if CLEAN_ON_BOOT
  uint16_t removed = deleteAllDataFiles();
  Serial.print(F("cleaned    : "));
  Serial.print(removed);
  Serial.println(F(" old file(s) removed"));
  #endif

  #if STORAGE_BACKEND == STORAGE_LITTLEFS
  uint32_t total = (uint32_t)LittleFS.totalBytes();
  uint32_t used  = (uint32_t)LittleFS.usedBytes();
  uint32_t freeB = total - used;
  Serial.print(F("volume     : "));
  Serial.print(total / 1024);
  Serial.print(F(" kB total, "));
  Serial.print(freeB / 1024);
  Serial.println(F(" kB free"));

  Serial.print(F("file size  : ~"));
  Serial.print(estimatedFileBytes() / 1024);
  Serial.print(F(" kB   ring keeps "));
  Serial.print(maxFiles);
  Serial.print(F(" -> "));
  Serial.print((uint32_t)maxFiles * FILE_PERIOD_S / 3600);
  Serial.println(F(" h of history"));

  /* How far this run will actually get on the space available. */
  uint32_t needBlocks = (uint32_t)(RUN_DURATION_S / FILE_PERIOD_S);
  if (!needBlocks) needBlocks = 1;
  uint32_t needBytes  = needBlocks * estimatedFileBytes();
  Serial.print(F("run needs  : "));
  Serial.print(needBytes / 1024);
  Serial.print(F(" kB for "));
  Serial.print(needBlocks);
  Serial.print(F(" files -> "));
  if (freeB >= needBytes) {
    Serial.println(F("FITS"));
  } else {
    uint32_t fits = freeB / estimatedFileBytes();
    Serial.print(F("ONLY "));
    Serial.print(fits);
    Serial.print(F(" files ("));
    Serial.print(fits * FILE_PERIOD_S / 60);
    Serial.println(F(" min) - ring buffer will prune"));
  }
  #endif
}
#else
static void PRINT_VOLUME_INFO(void) {}
#endif

void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { /* native USB, bounded wait */ }
  delay(200);

  Serial.println();
  Serial.println(F("=== Sensor logger ==="));
  Serial.print(F("rate       : 1 Hz\n"));
  Serial.print(F("tx period  : ")); Serial.print(TX_PERIOD_S);
  Serial.print(F(" s (")); Serial.print((int)ROWS_PER_FRAME);
  Serial.println(F(" rows/frame)"));
  Serial.print(F("file period: ")); Serial.print(FILE_PERIOD_S);
  Serial.print(F(" s (")); Serial.print((int)ROWS_PER_FILE);
  Serial.println(F(" rows/file)"));
  Serial.print(F("duration   : ")); Serial.print(RUN_DURATION_S);
  Serial.println(F(" s"));

#if STORAGE_BACKEND == STORAGE_LITTLEFS
  Serial.print(F("storage    : LittleFS ... "));
  if (!LittleFS.begin(true)) {          /* true = format if unmounted */
    Serial.println(F("FAILED"));
    while (1) delay(1000);
  }
  Serial.println(F("ok"));
  PRINT_VOLUME_INFO();
#elif STORAGE_BACKEND == STORAGE_SD
  Serial.print(F("storage    : SD ... "));
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("FAILED - check wiring and CS pin"));
    while (1) delay(1000);
  }
  Serial.println(F("ok"));
  PRINT_VOLUME_INFO();
#else
  Serial.println(F("storage    : SERIAL (no filesystem)"));
#endif

#if SEND_TO_OBC
  #if defined(ESP32)
  OBC_UART.begin(OBC_BAUD, SERIAL_8N1, OBC_RX_PIN, OBC_TX_PIN);
  #else
  OBC_UART.begin(OBC_BAUD);
  #endif
  Serial.print(F("obc link   : UART @ "));
  Serial.print(OBC_BAUD);
  #if USE_ACK
  Serial.println(F(" with ACK"));
  #else
  Serial.println(F(" fire-and-forget (heartbeat)"));
  #endif
#endif

  dummy_begin();
  tmp117_begin();
  bpx65_begin();
  bpw34_begin();
  n3163_begin();
  adr4525_begin();
  memscan_begin();

  Serial.print(F("samples    : "));
  Serial.print(dummy_count());
  Serial.println(F(" available before wrap"));
  Serial.println();

  openNextFile();
  lastSampleMs = millis();
}

void loop()
{
#if ACCEPT_COMMANDS && SEND_TO_OBC
  pollCommands();
#endif

  if (runComplete) return;

  if (millis() - lastSampleMs < SAMPLE_PERIOD_MS) return;
  lastSampleMs += SAMPLE_PERIOD_MS;        /* fixed cadence, no drift */

  writeRow();
  uptimeS++;
  dummy_advance();

  /* Heartbeat - from RAM, independent of the file. */
#if SEND_TO_OBC
  if (rowsInFrame >= (uint16_t)ROWS_PER_FRAME) sendHeartbeat();
#endif

  /* File rollover - independent of the heartbeat. */
  bool done = (RUN_DURATION_S && uptimeS >= (uint32_t)RUN_DURATION_S);

  if (rowsInFile >= (uint32_t)ROWS_PER_FILE || done) {
    closeCurrentFile();
    if (done) {
      runComplete = true;
      Serial.println();
      Serial.print(F("=== done: "));
      Serial.print(filesWritten);  Serial.print(F(" file(s), "));
      Serial.print(uptimeS);       Serial.print(F(" rows, "));
      Serial.print(framesSent);    Serial.print(F(" heartbeats, "));
      Serial.print(prunedFiles);   Serial.println(F(" pruned ==="));
      return;
    }
    openNextFile();
  }
}
