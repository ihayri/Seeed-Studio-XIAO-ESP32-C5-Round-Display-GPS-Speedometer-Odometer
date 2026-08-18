#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <TinyGPS++.h>

// ============================================================
// XIAO ESP32 C5 Round Display GPS Gauge
// GPS: D0 + D2
// Display: GC9A01
// Designed and build by Hayri
// ============================================================


// ============================================================
// DISPLAY PINS
// ============================================================

#define TFT_CS      D1
#define TFT_DC      D3
#define TFT_RST     -1
#define TFT_BL      D6

#define TFT_SCLK    D8
#define TFT_MISO    D9
#define TFT_MOSI    D10


// ============================================================
// GPS PINS
// ============================================================

// GPS TX -> XIAO D0
// GPS RX -> XIAO D2

#define GPS_RX_PIN  D0
#define GPS_TX_PIN  D2

// IMPORTANT:
// Your known-working GPS test uses 9600 baud.
#define GPS_BAUD    9600


// ============================================================
// COLORS
// ============================================================

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0


// ============================================================
// SCREEN SIZE
// ============================================================

#define SCREEN_W 240
#define SCREEN_H 240


// ============================================================
// LAYOUT CONFIGURATION
// ============================================================

// ------------------------------------------------------------
// SATELLITES
// ------------------------------------------------------------

#define SAT_X       80
#define SAT_Y       210


// ------------------------------------------------------------
// TIME
// ------------------------------------------------------------

#define TIME_X      75
#define TIME_Y      15


// ------------------------------------------------------------
// LATITUDE
// ------------------------------------------------------------

#define LAT_X       33
#define LAT_Y       57


// ------------------------------------------------------------
// LONGITUDE
// ------------------------------------------------------------

#define LON_X      125
#define LON_Y       57


// ------------------------------------------------------------
// SPEED
// ------------------------------------------------------------

#define SPEED_X     78
#define SPEED_Y     92

#define SPEED_TEXT_SIZE 5

#define SPEED_UNIT_X   95
#define SPEED_UNIT_Y  140


// ------------------------------------------------------------
// ODOMETER
// ------------------------------------------------------------

#define ODO_X       55
#define ODO_Y       175

#define ODO_TEXT_SIZE 3

#define ODO_UNIT_X  180
#define ODO_UNIT_Y  180


// ============================================================
// DISPLAY UPDATE RATE
// ============================================================

#define DISPLAY_INTERVAL_MS 250


// ============================================================
// GPS
// ============================================================

TinyGPSPlus gps;


// ============================================================
// GPS SERIAL
// ============================================================

HardwareSerial GPSSerial(1);


// ============================================================
// DISPLAY
// ============================================================

Adafruit_GC9A01A tft(
    TFT_CS,
    TFT_DC,
    TFT_RST
);


// ============================================================
// ODOMETER
// ============================================================

double odometerKm = 0.0;

bool havePreviousPosition = false;

double previousLat = 0.0;
double previousLng = 0.0;


// ============================================================
// ODOMETER FILTER
// ============================================================

const double MIN_SPEED_KMPH = 3.0;

const double MIN_VALID_STEP_METERS = 0.10;

const double MAX_VALID_STEP_METERS = 50.0;

const byte MAX_JUMP_REJECTS = 5;

byte consecutiveJumpRejects = 0;


// ============================================================
// DISPLAY TIMER
// ============================================================

unsigned long lastDisplayUpdate = 0;


// ============================================================
// GPS DEBUG
// ============================================================

// Keep this TRUE while testing.
// It prints the incoming NMEA stream to USB.

#define GPS_DEBUG true

unsigned long lastGPSDebug = 0;
unsigned long gpsCharsReceived = 0;


// ============================================================
// BULGARIAN GPS TIME
// ============================================================
//
// GPS provides UTC.
//
// Bulgaria:
//   Winter = UTC + 2
//   Summer = UTC + 3
//
// European DST:
//   Starts: last Sunday of March at 01:00 UTC
//   Ends:   last Sunday of October at 01:00 UTC
//
// This uses GPS date/time directly.
// NO NTP.
// NO WiFi.
// Works completely offline.
// ============================================================


// ------------------------------------------------------------
// Return weekday
//
// 0 = Sunday
// 1 = Monday
// ...
// 6 = Saturday
// ------------------------------------------------------------

int dayOfWeek(int year, int month, int day)
{
    if (month < 3)
    {
        month += 12;
        year--;
    }

    int K = year % 100;
    int J = year / 100;

    int h =
        (day +
         (13 * (month + 1)) / 5 +
         K +
         K / 4 +
         J / 4 +
         5 * J) % 7;

    // Zeller:
    // 0 = Saturday
    // 1 = Sunday
    // 2 = Monday
    // ...
    // Convert to:
    // 0 = Sunday
    // 1 = Monday
    // ...
    // 6 = Saturday

    return (h + 6) % 7;
}


// ------------------------------------------------------------
// Find last Sunday of a month
// ------------------------------------------------------------

int lastSunday(
    int year,
    int month,
    int lastDay
)
{
    int weekday = dayOfWeek(
        year,
        month,
        lastDay
    );

    return lastDay - weekday;
}


// ------------------------------------------------------------
// Get Bulgarian local time from GPS UTC
// ------------------------------------------------------------

bool getBulgarianTime(
    int &hour,
    int &minute,
    int &second
)
{
    if (!gps.time.isValid())
        return false;

    if (!gps.date.isValid())
        return false;


    // --------------------------------------------------------
    // GPS UTC
    // --------------------------------------------------------

    int year = gps.date.year();
    int month = gps.date.month();
    int day = gps.date.day();

    int utcHour = gps.time.hour();
    int utcMinute = gps.time.minute();
    int utcSecond = gps.time.second();


    // --------------------------------------------------------
    // Start with Bulgarian winter time
    // UTC + 2
    // --------------------------------------------------------

    int offset = 2;


    // --------------------------------------------------------
    // Determine DST
    // --------------------------------------------------------

    int marchLastSunday =
        lastSunday(
            year,
            3,
            31
        );

    int octoberLastSunday =
        lastSunday(
            year,
            10,
            31
        );


    bool daylightSaving = false;


    // --------------------------------------------------------
    // April through September
    // --------------------------------------------------------

    if (month > 3 && month < 10)
    {
        daylightSaving = true;
    }


    // --------------------------------------------------------
    // March
    //
    // DST starts on last Sunday at 01:00 UTC.
    // --------------------------------------------------------

    else if (month == 3)
    {
        if (day > marchLastSunday)
        {
            daylightSaving = true;
        }
        else if (
            day == marchLastSunday &&
            utcHour >= 1
        )
        {
            daylightSaving = true;
        }
    }


    // --------------------------------------------------------
    // October
    //
    // DST ends on last Sunday at 01:00 UTC.
    // --------------------------------------------------------

    else if (month == 10)
    {
        if (day < octoberLastSunday)
        {
            daylightSaving = true;
        }
        else if (
            day == octoberLastSunday &&
            utcHour < 1
        )
        {
            daylightSaving = true;
        }
    }


    if (daylightSaving)
        offset = 3;


    // --------------------------------------------------------
    // Apply timezone offset
    // --------------------------------------------------------

    hour = utcHour + offset;

    minute = utcMinute;
    second = utcSecond;


    // --------------------------------------------------------
    // Handle midnight rollover
    // --------------------------------------------------------

    if (hour >= 24)
    {
        hour -= 24;
    }


    return true;
}


// ============================================================
// READ GPS
// ============================================================

void readGPS()
{
    while (GPSSerial.available())
    {
        char c = GPSSerial.read();

        gpsCharsReceived++;

        // Feed EVERY character to TinyGPS++
        gps.encode(c);

        // Optional USB debug
        if (GPS_DEBUG)
        {
            Serial.write(c);
        }
    }
}


// ============================================================
// UPDATE ODOMETER
// ============================================================

void updateGPSOdometer()
{
    if (!gps.location.isUpdated())
        return;

    if (!gps.location.isValid())
        return;


    // Need at least 4 satellites

    if (!gps.satellites.isValid() ||
        gps.satellites.value() < 4)
    {
        havePreviousPosition = false;
        consecutiveJumpRejects = 0;
        return;
    }


    // Ignore movement below 3 km/h

    if (!gps.speed.isValid() ||
        gps.speed.kmph() < MIN_SPEED_KMPH)
    {
        return;
    }


    double lat = gps.location.lat();

    double lng = gps.location.lng();


    // --------------------------------------------------------
    // FIRST VALID POSITION
    // --------------------------------------------------------

    if (!havePreviousPosition)
    {
        previousLat = lat;

        previousLng = lng;

        havePreviousPosition = true;

        consecutiveJumpRejects = 0;

        return;
    }


    // --------------------------------------------------------
    // DISTANCE
    // --------------------------------------------------------

    double distanceMeters =
        TinyGPSPlus::distanceBetween(
            previousLat,
            previousLng,
            lat,
            lng
        );


    // --------------------------------------------------------
    // VALID STEP
    // --------------------------------------------------------

    if (
        distanceMeters >= MIN_VALID_STEP_METERS &&
        distanceMeters <= MAX_VALID_STEP_METERS
    )
    {
        odometerKm +=
            distanceMeters / 1000.0;

        previousLat = lat;

        previousLng = lng;

        consecutiveJumpRejects = 0;
    }


    // --------------------------------------------------------
    // GPS JUMP
    // --------------------------------------------------------

    else if (distanceMeters > MAX_VALID_STEP_METERS)
    {
        consecutiveJumpRejects++;

        if (
            consecutiveJumpRejects >=
            MAX_JUMP_REJECTS
        )
        {
            previousLat = lat;

            previousLng = lng;

            consecutiveJumpRejects = 0;
        }
    }
}


// ============================================================
// FORMAT ODOMETER
// ============================================================

void formatOdometer(
    double value,
    char *buffer
)
{
    if (value < 0.0)
        value = 0.0;

    if (value > 999.99)
        value = 999.99;


    int roundedValue =
        (int)((value * 100.0) + 0.5);


    int integerPart =
        roundedValue / 100;


    int fractionalPart =
        roundedValue % 100;


    snprintf(
        buffer,
        16,
        "%03d.%02d",
        integerPart,
        fractionalPart
    );
}


// ============================================================
// GET BULGARIAN TIME
// ============================================================

void getGPSTime(
    char *buffer
)
{
    int hour;
    int minute;
    int second;


    if (
        getBulgarianTime(
            hour,
            minute,
            second
        )
    )
    {
        snprintf(
            buffer,
            16,
            "%02d:%02d:%02d",
            hour,
            minute,
            second
        );
    }
    else
    {
        strcpy(
            buffer,
            "--:--:--"
        );
    }
}


// ============================================================
// DRAW SATELLITES
// ============================================================

void drawSatellites()
{
    tft.setTextSize(2);

    // Background color replaces old characters.

    tft.setTextColor(
        COLOR_WHITE,
        COLOR_BLACK
    );

    tft.setCursor(
        SAT_X,
        SAT_Y
    );


    char satBuffer[8];


    if (gps.satellites.isValid())
    {
        snprintf(
            satBuffer,
            sizeof(satBuffer),
            "Sats:%02d",
            gps.satellites.value()
        );
    }
    else
    {
        strcpy(
            satBuffer,
            "Sats:--"
        );
    }


    tft.print(
        satBuffer
    );
}


// ============================================================
// DRAW TIME
// ============================================================

void drawTime()
{
    char timeBuffer[16];


    getGPSTime(
        timeBuffer
    );


    tft.setTextSize(2);

    tft.setTextColor(
        COLOR_WHITE,
        COLOR_BLACK
    );


    tft.setCursor(
        TIME_X,
        TIME_Y
    );


    tft.print(
        timeBuffer
    );
}


// ============================================================
// DRAW LATITUDE
// ============================================================

void drawLatitude()
{
    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_WHITE,
        COLOR_BLACK
    );


    tft.setCursor(
        LAT_X,
        LAT_Y
    );


    if (gps.location.isValid())
    {
        tft.print("LAT ");

        tft.print(
            gps.location.lat(),
            6
        );
    }
    else
    {
        tft.print(
            "LAT --.------"
        );
    }
}


// ============================================================
// DRAW LONGITUDE
// ============================================================

void drawLongitude()
{
    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_WHITE,
        COLOR_BLACK
    );


    tft.setCursor(
        LON_X,
        LON_Y
    );


    if (gps.location.isValid())
    {
        tft.print("LON ");

        tft.print(
            gps.location.lng(),
            6
        );
    }
    else
    {
        tft.print(
            "LON --.------"
        );
    }
}


// ============================================================
// DRAW SPEED
// ============================================================

void drawSpeed()
{
    char speedBuffer[8];


    // --------------------------------------------------------
    // SPEED NUMBER
    // --------------------------------------------------------

    if (gps.speed.isValid())
    {
        int speed =
            (int)(gps.speed.kmph() + 0.5);


        // Always three characters:
        //
        // 000
        // 005
        // 027
        // 120

        snprintf(
            speedBuffer,
            sizeof(speedBuffer),
            "%03d",
            speed
        );
    }
    else
    {
        strcpy(
            speedBuffer,
            "---"
        );
    }


    tft.setTextSize(
        SPEED_TEXT_SIZE
    );


    tft.setTextColor(
        COLOR_WHITE,
        COLOR_BLACK
    );


    tft.setCursor(
        SPEED_X,
        SPEED_Y
    );


    tft.print(
        speedBuffer
    );


    // --------------------------------------------------------
    // km/h
    // --------------------------------------------------------

    tft.setTextSize(2);

    tft.setTextColor(
        COLOR_WHITE,
        COLOR_BLACK
    );


    tft.setCursor(
        SPEED_UNIT_X,
        SPEED_UNIT_Y
    );


    tft.print(
        "km/h"
    );
}


// ============================================================
// DRAW ODOMETER
// ============================================================

void drawOdometer()
{
    char odometerBuffer[16];


    formatOdometer(
        odometerKm,
        odometerBuffer
    );


    tft.setTextSize(
        ODO_TEXT_SIZE
    );


    tft.setTextColor(
        COLOR_WHITE,
        COLOR_BLACK
    );


    tft.setCursor(
        ODO_X,
        ODO_Y
    );


    tft.print(
        odometerBuffer
    );


    // --------------------------------------------------------
    // km
    // --------------------------------------------------------

    tft.setTextSize(2);

    tft.setTextColor(
        COLOR_WHITE,
        COLOR_BLACK
    );


    tft.setCursor(
        ODO_UNIT_X,
        ODO_UNIT_Y
    );


    tft.print(
        "km"
    );
}


// ============================================================
// DRAW STATIC SCREEN
// ============================================================
//
// This is the ONLY place where we clear the entire screen.
//
// After this, NO fillRect() or fillScreen() is used for
// changing data.
// ============================================================

void drawStaticScreen()
{
    tft.fillScreen(
        COLOR_BLACK
    );


    // --------------------------------------------------------
    // TOP DIVIDER
    // --------------------------------------------------------

    tft.drawLine(
        25,
        40,
        215,
        40,
        COLOR_WHITE
    );


    // --------------------------------------------------------
    // BOTTOM DIVIDER
    // --------------------------------------------------------

    tft.drawLine(
        25,
        165,
        215,
        165,
        COLOR_WHITE
    );


    // --------------------------------------------------------
    // INITIAL DATA
    // --------------------------------------------------------

    drawSatellites();

    drawTime();

    drawLatitude();

    drawLongitude();

    drawSpeed();

    drawOdometer();
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    // --------------------------------------------------------
    // USB SERIAL
    // --------------------------------------------------------

    Serial.begin(
        115200
    );


    delay(1000);


    Serial.println();

    Serial.println(
        "=============================="
    );

    Serial.println(
        "XIAO ESP32-C5 GPS + DISPLAY"
    );

    Serial.println(
        "=============================="
    );


    // --------------------------------------------------------
    // GPS
    // --------------------------------------------------------

    Serial.println(
        "Starting GPS..."
    );


    GPSSerial.begin(
        GPS_BAUD,
        SERIAL_8N1,
        GPS_RX_PIN,
        GPS_TX_PIN
    );


    Serial.println(
        "GPS UART started"
    );

    Serial.println(
        "GPS: 9600 baud"
    );

    Serial.println(
        "GPS TX -> D0"
    );

    Serial.println(
        "GPS RX -> D2"
    );


    // --------------------------------------------------------
    // BACKLIGHT
    // --------------------------------------------------------

    pinMode(
        TFT_BL,
        OUTPUT
    );


    digitalWrite(
        TFT_BL,
        HIGH
    );


    // --------------------------------------------------------
    // SPI
    // --------------------------------------------------------

    Serial.println(
        "Starting SPI..."
    );


    SPI.begin(
        TFT_SCLK,
        TFT_MISO,
        TFT_MOSI,
        TFT_CS
    );


    // --------------------------------------------------------
    // DISPLAY
    // --------------------------------------------------------

    Serial.println(
        "Starting display..."
    );


    tft.begin();


    delay(500);


    tft.invertDisplay(
        true
    );


    // KEEP EXACTLY THE SAME ROTATION
    tft.setRotation(
        3
    );


    // --------------------------------------------------------
    // INITIAL SCREEN
    // --------------------------------------------------------

    tft.fillScreen(
        COLOR_BLACK
    );


    drawStaticScreen();


    Serial.println(
        "Display OK"
    );


    Serial.println(
        "GPS OK"
    );


    Serial.println(
        "Waiting for GPS data..."
    );
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    // --------------------------------------------------------
    // GPS
    // --------------------------------------------------------

    readGPS();


    // --------------------------------------------------------
    // ODOMETER
    // --------------------------------------------------------

    updateGPSOdometer();


    // --------------------------------------------------------
    // DISPLAY
    // --------------------------------------------------------

    if (
        millis() -
        lastDisplayUpdate >=
        DISPLAY_INTERVAL_MS
    )
    {
        lastDisplayUpdate =
            millis();


        // NO fillScreen()
        // NO fillRect()
        //
        // Background color automatically removes
        // the previous characters.

        drawSatellites();

        drawTime();

        drawLatitude();

        drawLongitude();

        drawSpeed();

        drawOdometer();
    }
}
