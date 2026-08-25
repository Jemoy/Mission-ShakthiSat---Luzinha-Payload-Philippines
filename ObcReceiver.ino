/* =====================================================================
 *  ObcReceiver.ino
 *  ---------------------------------------------------------------------
 *  Main-OBC side of the payload link. Receives the payload's plain-text
 *  file frames, verifies them, and reports.
 *
 *  Frame format:
 *      <FILE 639_000030>
 *      sec,tmp0_C,...            <- column header
 *      0,-11.1,...               <- data rows
 *      <END 639_000030 1994 A3F19C22>
 *                     |    |
 *                     |    CRC32 of the body, poly 0xEDB88320
 *                     byte count of the body
 *
 *  The body is every byte between the newline that ends the <FILE> line
 *  and the '<' of <END>, including each row's own line terminator.
 *
 *  STREAMING, NOT BUFFERED
 *  A heartbeat is ~2 kB but a RESEND of an hourly file is ~230 kB, far
 *  more than RAM. So the body is never held: bytes are fed into a
 *  running CRC as they arrive, and only one line is buffered at a time.
 *  A line is only known to be the <END> terminator once it is complete,
 *  which is why the previous line is folded into the CRC one line late.
 *
 *  Type commands into the Serial Monitor to talk back to the payload:
 *      LIST                  stored files and sizes
 *      STATUS                uptime, counters, free space
 *      RESEND 639_003600     stream a stored file back
 * ===================================================================== */

#include <Arduino.h>

/* =====================================================================
 *  CONFIGURATION
 * ===================================================================== */

#define PAYLOAD_UART     Serial1
#define PAYLOAD_BAUD     115200

#if defined(ESP32)
  #define PAYLOAD_RX_PIN 16      /* wire to the payload's TX */
  #define PAYLOAD_TX_PIN 17      /* wire to the payload's RX */
#endif

/* Heartbeat cadence. Filenames step by this many seconds, so a larger
 * jump means a beat was missed and tells you exactly which seconds.   */
#define EXPECTED_STEP_S  30
#define LATE_WARN_MS     40000UL   /* warn if no frame for this long   */

#define LINE_MAX         320       /* longest line we expect           */

/* ---------------------------------------------------------------------
 *  Showing file contents
 *
 *    PRINT_NONE      verification summary only
 *    PRINT_ALL       every row. Fine for a 30-row heartbeat, but a
 *                    RESEND of an hourly file is 3600 rows and will
 *                    bury the monitor.
 *    PRINT_HEADTAIL  first HEAD_ROWS and last TAIL_ROWS of each frame,
 *                    which is enough to see the shape and the time
 *                    range of any file regardless of size.
 * ------------------------------------------------------------------- */
#define PRINT_NONE       0
#define PRINT_ALL        1
#define PRINT_HEADTAIL   2

#define PRINT_ROWS       PRINT_HEADTAIL
#define HEAD_ROWS        4
#define TAIL_ROWS        3

/* =====================================================================
 *  CRC32  (reflected polynomial 0xEDB88320) - incremental
 * ===================================================================== */
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

/* =====================================================================
 *  STATE
 * ===================================================================== */
enum RxState { WAIT_FILE, IN_BODY };

static RxState  state = WAIT_FILE;
static char     line[LINE_MAX];
static uint16_t lineLen = 0;

/* One line is held back: we cannot fold it into the CRC until we know
 * it is not the <END> terminator.                                     */
static char     held[LINE_MAX];
static uint16_t heldLen = 0;
static bool     haveHeld = false;

static char     fileName[32];
static uint32_t bodyBytes = 0;
static uint32_t bodyCrc = 0;
static uint32_t rowCount = 0;

static uint32_t filesOk = 0, filesBad = 0, gaps = 0;
static uint32_t lastNumber = 0;
static bool     haveLast = false;
static uint32_t lastFrameMs = 0;
static bool     warnedLate = false;

/* =====================================================================
 *  HELPERS
 * ===================================================================== */

/* Numeric part of 639_003600 -> 3600 */
static uint32_t nameNumber(const char *n)
{
  const char *u = strchr(n, '_');
  return u ? (uint32_t)strtoul(u + 1, nullptr, 10) : 0;
}

#if PRINT_ROWS == PRINT_HEADTAIL
/* Rolling window of the most recent rows. Only TAIL_ROWS lines are kept,
 * so a 3600-row file costs the same RAM as a 30-row one.              */
static char     tailBuf[TAIL_ROWS][LINE_MAX];
static uint16_t tailLen[TAIL_ROWS];
static uint8_t  tailNext = 0;
static uint32_t tailCount = 0;

static void tailPush(const char *s, uint16_t n)
{
  if (n >= LINE_MAX) n = LINE_MAX - 1;
  memcpy(tailBuf[tailNext], s, n);
  tailLen[tailNext] = n;
  tailNext = (uint8_t)((tailNext + 1) % TAIL_ROWS);
  tailCount++;
}

static void tailFlush(uint32_t total)
{
  uint8_t n = (uint8_t)((tailCount < TAIL_ROWS) ? tailCount : TAIL_ROWS);
  if (total > (uint32_t)(HEAD_ROWS + n)) Serial.println(F("      ..."));
  uint8_t start = (uint8_t)((tailNext + TAIL_ROWS - n) % TAIL_ROWS);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t k = (uint8_t)((start + i) % TAIL_ROWS);
    /* Skip anything already shown as part of the head. */
    if (total - n + i < (uint32_t)HEAD_ROWS) continue;
    tailBuf[k][tailLen[k]] = 0;
    Serial.print(F("      "));
    Serial.println(tailBuf[k]);
  }
  tailCount = 0;
  tailNext = 0;
}
#endif

static void printBodyLine(const char *s, uint16_t n, uint32_t index)
{
#if PRINT_ROWS == PRINT_ALL
  (void)index;
  char tmp[LINE_MAX];
  memcpy(tmp, s, n); tmp[n] = 0;
  Serial.print(F("      "));
  Serial.println(tmp);
#elif PRINT_ROWS == PRINT_HEADTAIL
  if (index < (uint32_t)HEAD_ROWS) {
    char tmp[LINE_MAX];
    memcpy(tmp, s, n); tmp[n] = 0;
    Serial.print(F("      "));
    Serial.println(tmp);
  }
  tailPush(s, n);
#else
  (void)s; (void)n; (void)index;
#endif
}

static void foldHeld(void)
{
  if (!haveHeld) return;
  bodyCrc = crc32_update(bodyCrc, (const uint8_t *)held, heldLen);
  bodyBytes += heldLen;

  /* Strip the line terminator for display only; the CRC covers the
   * bytes exactly as they arrived.                                    */
  uint16_t show = heldLen;
  while (show && (held[show - 1] == '\n' || held[show - 1] == '\r')) show--;
  printBodyLine(held, show, rowCount);

  rowCount++;
  haveHeld = false;
  heldLen = 0;
}

static void startBody(const char *name)
{
  strncpy(fileName, name, sizeof(fileName) - 1);
  fileName[sizeof(fileName) - 1] = 0;
  bodyCrc = crc32_init();
  bodyBytes = 0;
  rowCount = 0;
  haveHeld = false;
  heldLen = 0;
  state = IN_BODY;
}

static void finishBody(const char *endLine)
{
  char endName[32] = {0};
  unsigned long declared = 0, declaredCrc = 0;

  int fields = sscanf(endLine, "<END %31s %lu %lx>", endName, &declared, &declaredCrc);
  /* Some parsers choke on the trailing '>' being swallowed by %s; the
   * name never contains '>' so trim it defensively.                   */
  char *gt = strchr(endName, '>');
  if (gt) *gt = 0;

  uint32_t actual = crc32_final(bodyCrc);
  bool ok = (fields == 3) &&
            (bodyBytes == declared) &&
            (actual == declaredCrc) &&
            (strcmp(fileName, endName) == 0);

  uint32_t now = millis();
  uint32_t dt = lastFrameMs ? (now - lastFrameMs) : 0;
  lastFrameMs = now;
  warnedLate = false;

  /* Filenames are the sequence number: a step larger than one period
   * means a heartbeat was lost, and says exactly which seconds.       */
  uint32_t num = nameNumber(fileName);
  if (haveLast && num > lastNumber) {
    uint32_t step = num - lastNumber;
    if (step != EXPECTED_STEP_S) {
      gaps++;
      Serial.print(F("  ** gap: "));
      Serial.print(lastNumber);
      Serial.print(F(" -> "));
      Serial.print(num);
      Serial.print(F("  ("));
      Serial.print(step / EXPECTED_STEP_S - 1);
      Serial.println(F(" missed)"));
    }
  }
  if (num) { lastNumber = num; haveLast = true; }

#if PRINT_ROWS == PRINT_HEADTAIL
  tailFlush(rowCount);
#endif

  Serial.print(ok ? F("[OK ] ") : F("[BAD] "));
  Serial.print(fileName);
  Serial.print(F("  "));
  Serial.print(bodyBytes);
  Serial.print(F(" B  "));
  Serial.print(rowCount);
  Serial.print(F(" lines"));
  if (dt) { Serial.print(F("  +")); Serial.print(dt / 1000.0f, 1); Serial.print(F("s")); }

  if (!ok) {
    if (fields != 3)                 Serial.print(F("  malformed END"));
    if (bodyBytes != declared)     { Serial.print(F("  size!=")); Serial.print(declared); }
    if (actual != declaredCrc) {
      char cbuf[32];
      snprintf(cbuf, sizeof(cbuf), "  crc %08lX!=%08lX",
               (unsigned long)actual, (unsigned long)declaredCrc);
      Serial.print(cbuf);
    }
    if (strcmp(fileName, endName)) { Serial.print(F("  name!=")); Serial.print(endName); }
  }
  Serial.println();

  ok ? filesOk++ : filesBad++;
  state = WAIT_FILE;
}

/* =====================================================================
 *  BYTE-AT-A-TIME PARSER
 * ===================================================================== */
static void handleLine(void)
{
  line[lineLen] = 0;                 /* NUL only for inspection */

  if (state == WAIT_FILE) {
    if (strncmp(line, "<FILE ", 6) == 0) {
      char name[32] = {0};
      sscanf(line, "<FILE %31[^>]>", name);
      startBody(name);
#if PRINT_ROWS != PRINT_NONE
      Serial.print(F("--- "));
      Serial.print(name);
      Serial.println(F(" ---"));
#endif
      return;
    }
    /* Command replies and anything else the payload volunteers. */
    if (line[0] == '<' || line[0] == '6') {
      if (lineLen > 1) Serial.println(line);
    }
    return;
  }

  /* IN_BODY */
  if (strncmp(line, "<END ", 5) == 0) {
    /* The held line is the last row and DOES belong to the body. */
    foldHeld();
    finishBody(line);
    return;
  }

  /* A previous line is now known not to be the terminator. */
  foldHeld();
  memcpy(held, line, lineLen);
  heldLen = lineLen;
  haveHeld = true;
}

static void pollPayload(void)
{
  while (PAYLOAD_UART.available()) {
    char c = (char)PAYLOAD_UART.read();

    if (lineLen < LINE_MAX - 1) line[lineLen++] = c;
    else lineLen = 0;                /* overlong: resynchronise */

    if (c == '\n') {
      handleLine();
      lineLen = 0;
    }
  }
}

/* =====================================================================
 *  CONSOLE -> PAYLOAD
 * ===================================================================== */
static char consoleBuf[64];
static uint8_t consoleLen = 0;

static void printHelp(void)
{
  Serial.println(F("commands: LIST | STATUS | RESEND <name> | ? for stats"));
}

static void printStats(void)
{
  Serial.println(F("--- receiver ---"));
  Serial.print(F("  files ok     : ")); Serial.println(filesOk);
  Serial.print(F("  files bad    : ")); Serial.println(filesBad);
  Serial.print(F("  heartbeat gaps: ")); Serial.println(gaps);
  Serial.print(F("  last name    : "));
  if (haveLast) Serial.println(lastNumber); else Serial.println(F("none"));
}

static void pollConsole(void)
{
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (!consoleLen) continue;
      consoleBuf[consoleLen] = 0;
      if (consoleBuf[0] == '?')      printStats();
      else if (!strcmp(consoleBuf, "help")) printHelp();
      else {
        PAYLOAD_UART.println(consoleBuf);   /* forward verbatim */
        Serial.print(F("> "));
        Serial.println(consoleBuf);
      }
      consoleLen = 0;
      continue;
    }
    if (consoleLen < sizeof(consoleBuf) - 1) consoleBuf[consoleLen++] = c;
    else consoleLen = 0;
  }
}

/* =====================================================================
 *  SETUP / LOOP
 * ===================================================================== */
void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }
  delay(200);

#if defined(ESP32)
  PAYLOAD_UART.begin(PAYLOAD_BAUD, SERIAL_8N1, PAYLOAD_RX_PIN, PAYLOAD_TX_PIN);
#else
  PAYLOAD_UART.begin(PAYLOAD_BAUD);
#endif

  Serial.println();
  Serial.println(F("=== OBC receiver ==="));
  Serial.print(F("link       : UART @ ")); Serial.println(PAYLOAD_BAUD);
  Serial.print(F("expect     : one frame every ")); Serial.print(EXPECTED_STEP_S);
  Serial.println(F(" s"));
  Serial.println(F("verify     : byte count + CRC32 (0xEDB88320), streamed"));
  printHelp();
  Serial.println();
}

void loop()
{
  pollPayload();
  pollConsole();

  /* A missing heartbeat is the payload's liveness signal failing, so
   * say so rather than waiting silently.                              */
  if (lastFrameMs && !warnedLate && (millis() - lastFrameMs) > LATE_WARN_MS) {
    warnedLate = true;
    Serial.print(F("  ** no frame for "));
    Serial.print((millis() - lastFrameMs) / 1000);
    Serial.println(F(" s - payload may be down"));
  }
}
