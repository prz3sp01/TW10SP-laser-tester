/*******************************************************************************************
 *
 * TW10SP Laser Tester v1.0.0
 *
 * Description:
 * A cross-platform (Windows/Linux) desktop application for testing, 
 * monitoring, and visualizing data from the TW10SP laser distance sensor.
 * Communicates via UART/USB-UART using a continuous ASCII stream mode ('contis') 
 * at 9600 8N1 to achieve high-speed, real-time measurements.
 * GUI built with Raylib.
 *
 * Dependencies:
 * - raylib (www.raylib.com)
 * - Windows API (for Windows builds) or POSIX (for Linux builds)
 *
 * Author:
 * Karol "prz3sp01" Przespolewski (karol@przespol.eu)
 *
 * Copyright (c) 2026 Karol Przespolewski. All Rights Reserved.
 *
 ********************************************************************************************/

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #include <windows.h>
    #include <mmsystem.h>
    #undef near
    #undef far
    #undef PlaySound
    #undef DrawText
    #undef CloseWindow
    #undef ShowCursor
    #undef Rectangle
    typedef HANDLE SerialHandle;
    typedef HANDLE ThreadHandle;
    typedef CRITICAL_SECTION MutexType;
    #define INVALID_SERIAL_HANDLE INVALID_HANDLE_VALUE
    #define MUTEX_INIT(m) InitializeCriticalSection(&m)
    #define MUTEX_LOCK(m) EnterCriticalSection(&m)
    #define MUTEX_UNLOCK(m) LeaveCriticalSection(&m)
    #define SLEEP_MS(ms) Sleep(ms)
    long long get_time_ms() { return (long long)GetTickCount64(); }
    const char* available_ports[] = { "\\\\.\\COM1", "\\\\.\\COM2", "\\\\.\\COM3", "\\\\.\\COM4", "\\\\.\\COM5" };
    const char* display_ports[] = { "COM1", "COM2", "COM3", "COM4", "COM5" };
#else
    #include <pthread.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <termios.h>
    #include <time.h>
    typedef int SerialHandle;
    typedef pthread_t ThreadHandle;
    typedef pthread_mutex_t MutexType;
    #define INVALID_SERIAL_HANDLE -1
    #define MUTEX_INIT(m) pthread_mutex_init(&m, NULL)
    #define MUTEX_LOCK(m) pthread_mutex_lock(&m)
    #define MUTEX_UNLOCK(m) pthread_mutex_unlock(&m)
    #define SLEEP_MS(ms) usleep((ms) * 1000)
    long long get_time_ms() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (long long)ts.tv_sec * 1000LL + (ts.tv_nsec / 1000000LL);
    }
    const char* available_ports[] = { "/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyUSB2", "/dev/ttyACM0", "/dev/ttyS0" };
    const char* display_ports[] = { "ttyUSB0", "ttyUSB1", "ttyUSB2", "ttyACM0", "ttyS0" };
#endif

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NUM_PORTS (sizeof(available_ports)/sizeof(available_ports[0]))
#define SAMPLE_RATE 44100
#define MAX_LOGS 5
#define HISTORY_LEN 150

typedef enum { LANG_PL = 0, LANG_NL, LANG_EN } Language;

const char* txt_panel[]    = { "PANEL STEROWANIA TW10SP", "TW10SP BEDIENINGSPANEEL", "TW10SP CONTROL PANEL" };
const char* txt_start[]    = { "START POMIARU", "START METING", "START MEASURE" };
const char* txt_stop[]     = { "ZATRZYMAJ", "STOPPEN", "STOP" };
const char* txt_pause[]    = { "PAUZA", "PAUZE", "PAUSED" };
const char* txt_err_open[] = { "[BLAD] Nie mozna otworzyc portu. Jest zajety?", "[FOUT] Poort kan niet worden geopend.", "[ERROR] Cannot open port. Is it busy?" };
const char* txt_port_rel[] = { "Port zwolniony", "Poort vrijgegeven", "Port released" };
const char* txt_active[]   = { "POMIAR AKTYWNY", "METING ACTIEF", "MEASURE ACTIVE" };
const char* txt_no_echo[]  = { "BRAK ECHA (ZAKRES)", "GEEN ECHO (BEREIK)", "OUT OF RANGE" };
const char* txt_lost[]     = { "BRAK KOMUNIKACJI", "VERBINDING VERLOREN", "CONNECTION LOST" };
const char* txt_closed[]   = { "PORT ZAMKNIETY", "POORT GESLOTEN", "PORT CLOSED" };
const char* txt_logs[]     = { "TERMINAL SYSTEMOWY", "SYSTEEM LOGBOEK", "SYSTEM TERMINAL" };
const char* txt_rx[]       = { "RX ODCZYT:", "RX LEZING:", "RX RATE:" };
const char* txt_fps[]      = { "RAMKI/s", "FRAMES/s", "FRAMES/s" };

// Etykiety nad przyciskami (3 języki)
const char* lbl_port[]     = { "PORT COM", "COM POORT", "COM PORT" };
const char* lbl_buzzer[]   = { "BUZZER / AUDIO", "BUZZER / GELUID", "BUZZER / AUDIO" };
const char* lbl_lang[]     = { "JEZYK / LANGUAGE", "TAAL / LANGUAGE", "LANGUAGE" };

const char* lang_names[]   = { "Polski (PL)", "Nederlands (NL)", "English (EN)" };

const char* txt_help_title[] = { 
    "INFORMACJE TECHNICZNE / SPECYFIKACJA", 
    "TECHNISCHE INFORMATIE / SPECIFICATIE", 
    "TECHNICAL INFORMATION / SPECIFICATION" 
};
const char* txt_help_body[] = {
    "Wspierany czujnik: TW10SP Laser Distance Sensor\n"
    "Tryb pracy: Ciągły strumień ASCII (Continuous Stream 'contis')\n"
    "Parametry UART: 9600 Baud, 8 Data bits, 1 Stop bit, No Parity (8N1)\n\n"
    "SPOSÓB PODŁĄCZENIA HARDWARE:\n"
    "- Moduł TW10SP pracuje w standardzie logicznym TTL (3.3V / 5V).\n"
    "- Do podłączenia z PC wymagany jest konwerter USB-UART (np. CP2102, CH340, FTDI).\n"
    "- VCC -> 5V (lub 3.3V w zależności od wersji), GND -> Masa wspólna.\n"
    "- TX czujnika -> RX konwertera, RX czujnika -> TX konwertera.\n\n"
    "Zalecenia: Dla maksymalnej częstotliwości celuj w jasne, odbijające powierzchnie.",

    "Ondersteunde sensor: TW10SP Laser Afstandsdetector\n"
    "Werkingsmodus: Continue ASCII-stroom ('contis')\n"
    "UART-parameters: 9600 Baud, 8 Databits, 1 Stopbit, Geen Pariteit (8N1)\n\n"
    "HARDWARE VERBINDINGSINSTRUCTIES:\n"
    "- De TW10SP module werkt op TTL-logicaniveau (3.3V / 5V).\n"
    "- Een USB-UART converter (bijv. CP2102, CH340, FTDI) is vereist voor PC-aansluiting.\n"
    "- VCC -> 5V, GND -> Gedeelde aarde.\n"
    "- Sensor TX -> Converter RX, Sensor RX -> Converter TX.\n\n"
    "Aanbeveling: Richt op heldere, reflecterende oppervlakken voor optimale prestaties.",

    "Supported sensor: TW10SP Laser Distance Sensor\n"
    "Operation Mode: Continuous ASCII Stream ('contis')\n"
    "UART Parameters: 9600 Baud, 8 Data bits, 1 Stop bit, No Parity (8N1)\n\n"
    "HARDWARE CONNECTION GUIDE:\n"
    "- TW10SP module operates on TTL logic levels (3.3V / 5V).\n"
    "- A USB-UART bridge converter (e.g., CP2102, CH340, FTDI) is required for PC connection.\n"
    "- VCC -> 5V / 3.3V, GND -> Common Ground.\n"
    "- Sensor TX -> Converter RX, Sensor RX -> Converter TX.\n\n"
    "Tip: For maximum data rate, target bright and highly reflective surfaces."
};
const char* txt_close_btn[] = { "ZAMKNIJ", "SLUITEN", "CLOSE" };

typedef struct {
    int port_idx;
    int distance_mm;
    bool connected;        
    bool valid_distance;   
    bool new_sample;
    bool running;
    bool measuring;
    Language lang;
    bool sound_on;
    bool show_help;
    bool lang_dropdown_open;
    unsigned int total_frames;
    char logs[MAX_LOGS][128];
    int log_count;
    int history[HISTORY_LEN];
    int history_head;
    MutexType lock;
} LaserData;

LaserData g_laser;

void add_log(const char* msg) {
    MUTEX_LOCK(g_laser.lock);
    if (g_laser.log_count < MAX_LOGS) {
        strcpy(g_laser.logs[g_laser.log_count], msg);
        g_laser.log_count++;
    } else {
        for (int i = 1; i < MAX_LOGS; i++) strcpy(g_laser.logs[i-1], g_laser.logs[i]);
        strcpy(g_laser.logs[MAX_LOGS-1], msg);
    }
    MUTEX_UNLOCK(g_laser.lock);
}

void clear_logs_safe() {
    MUTEX_LOCK(g_laser.lock);
    g_laser.log_count = 0;
    MUTEX_UNLOCK(g_laser.lock);
}

SerialHandle serial_open(const char* portname) {
#ifdef _WIN32
    HANDLE hComm = CreateFileA(portname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hComm == INVALID_HANDLE_VALUE) return INVALID_SERIAL_HANDLE;
    DCB dcb = {0}; dcb.DCBlength = sizeof(dcb);
    GetCommState(hComm, &dcb);
    dcb.BaudRate = CBR_9600; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    SetCommState(hComm, &dcb);
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD; 
    timeouts.ReadTotalTimeoutConstant = 0; 
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComm, &timeouts);
    return hComm;
#else
    int fd = open(portname, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return INVALID_SERIAL_HANDLE;
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) { close(fd); return INVALID_SERIAL_HANDLE; }
    cfsetospeed(&tty, B9600); cfsetispeed(&tty, B9600);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0; 
    tty.c_cc[VTIME] = 0; 
    if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); return INVALID_SERIAL_HANDLE; }
    return fd;
#endif
}

void serial_close(SerialHandle fd) {
#ifdef _WIN32
    CloseHandle(fd);
#else
    close(fd);
#endif
}

void serial_flush(SerialHandle fd) {
#ifdef _WIN32
    PurgeComm(fd, PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
    tcflush(fd, TCIOFLUSH);
#endif
}

int serial_write(SerialHandle fd, const void* buf, int len) {
#ifdef _WIN32
    DWORD w; WriteFile(fd, buf, len, &w, NULL); return (int)w;
#else
    return write(fd, buf, len);
#endif
}

int serial_read(SerialHandle fd, void* buf, int len) {
#ifdef _WIN32
    DWORD r; ReadFile(fd, buf, len, &r, NULL); return (int)r;
#else
    return read(fd, buf, len);
#endif
}

#ifdef _WIN32
DWORD WINAPI uart_worker(LPVOID arg) {
#else
void* uart_worker(void* arg) {
#endif
    SerialHandle fd = INVALID_SERIAL_HANDLE;
    int current_port_idx = -1;
    bool open_error_logged = false;
    char rx_buffer[256];
    int rx_idx = 0;
    long long last_rx_time = 0;
    bool is_streaming = false;

    while (g_laser.running) {
        bool should_measure; int target_port_idx; Language lng;
        MUTEX_LOCK(g_laser.lock);
        should_measure = g_laser.measuring;
        target_port_idx = g_laser.port_idx;
        lng = g_laser.lang;
        MUTEX_UNLOCK(g_laser.lock);

        if (current_port_idx != target_port_idx) {
            if (fd != INVALID_SERIAL_HANDLE) { serial_close(fd); fd = INVALID_SERIAL_HANDLE; }
            current_port_idx = target_port_idx;
            open_error_logged = false;
            is_streaming = false;
        }

        if (should_measure) {
            if (fd == INVALID_SERIAL_HANDLE) {
                fd = serial_open(available_ports[current_port_idx]);
                if (fd == INVALID_SERIAL_HANDLE) {
                    MUTEX_LOCK(g_laser.lock); g_laser.connected = false; MUTEX_UNLOCK(g_laser.lock);
                    if (!open_error_logged) { add_log(txt_err_open[lng]); open_error_logged = true; }
                    SLEEP_MS(500);
                    continue;
                } else {
                    add_log(TextFormat("[ASCII] OK -> %s", display_ports[current_port_idx]));
                    open_error_logged = false;
                    is_streaming = false;
                }
            }

            if (!is_streaming) {
                serial_flush(fd);
                const char* start_cmd = "contis\r\n";
                serial_write(fd, start_cmd, strlen(start_cmd));
                is_streaming = true;
                rx_idx = 0;
                last_rx_time = get_time_ms();
            }

            char temp[64];
            int r = serial_read(fd, temp, sizeof(temp));
            if (r > 0) {
                for (int i = 0; i < r; i++) {
                    if (temp[i] == '\n') {
                        rx_buffer[rx_idx] = '\0';
                        float dist_m = -1.0f;
                        if (sscanf(rx_buffer, "D:%fm", &dist_m) == 1) {
                            int dist_mm = (int)(dist_m * 1000.0f);
                            MUTEX_LOCK(g_laser.lock);
                            g_laser.connected = true;
                            g_laser.total_frames++;
                            if (dist_mm > 0 && dist_mm < 45000) {
                                g_laser.distance_mm = dist_mm;
                                g_laser.valid_distance = true;
                                g_laser.new_sample = true;
                                g_laser.history[g_laser.history_head] = dist_mm;
                                g_laser.history_head = (g_laser.history_head + 1) % HISTORY_LEN;
                            }
                            MUTEX_UNLOCK(g_laser.lock);
                        }
                        rx_idx = 0;
                        last_rx_time = get_time_ms();
                    } else if (temp[i] != '\r') {
                        if (rx_idx < sizeof(rx_buffer) - 1) rx_buffer[rx_idx++] = temp[i];
                    }
                }
            } else {
                SLEEP_MS(2);
            }

            if (get_time_ms() - last_rx_time > 3000) {
                MUTEX_LOCK(g_laser.lock);
                g_laser.valid_distance = false;
                MUTEX_UNLOCK(g_laser.lock);
            }
        } else {
            if (fd != INVALID_SERIAL_HANDLE) {
                const char* stop_cmd = "C\r\n";
                serial_write(fd, stop_cmd, strlen(stop_cmd));
                const unsigned char modbus_stop[] = { 0x01, 0x03, 0x00, 0x0F, 0x00, 0x02, 0xF4, 0x08 };
                serial_write(fd, modbus_stop, sizeof(modbus_stop));
                SLEEP_MS(50);
                serial_close(fd); 
                fd = INVALID_SERIAL_HANDLE;
                is_streaming = false;
                add_log(TextFormat("[SYSTEM] %s", txt_port_rel[lng]));
                MUTEX_LOCK(g_laser.lock); g_laser.connected = false; g_laser.valid_distance = false; MUTEX_UNLOCK(g_laser.lock);
            }
            SLEEP_MS(50);
        }
    }
    if (fd != INVALID_SERIAL_HANDLE) serial_close(fd);
    return 0;
}

Sound create_laser_blip(void) {
    Wave wave = { 0 }; wave.frameCount = SAMPLE_RATE / 12; wave.sampleRate = SAMPLE_RATE;
    wave.sampleSize = 16; wave.channels = 1;
    short *samples = (short*)malloc(wave.frameCount * sizeof(short));
    for (int i = 0; i < (int)wave.frameCount; i++) {
        float t = (float)i / SAMPLE_RATE;
        float freq = 1600.0f - 800.0f * (t / 0.08f);
        samples[i] = (short)(sinf(2.0f * PI * freq * t) * (1.0f - (t / 0.08f)) * 10000.0f);
    }
    wave.data = samples; Sound snd = LoadSoundFromWave(wave); UnloadWave(wave);
    return snd;
}

bool DrawCustomButton(Rectangle rect, const char* text, Color colorMain, Color colorHover, int fontSize) {
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, rect);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    DrawRectangleRounded(rect, 0.25f, 4, hover ? colorHover : colorMain);
    DrawRectangleRoundedLines(rect, 0.25f, 4, (Color){100, 130, 160, 120});
    int textW = MeasureText(text, fontSize);
    DrawText(text, rect.x + (rect.width - textW)/2, rect.y + (rect.height - fontSize)/2, fontSize, WHITE);
    return clicked;
}

void Draw7SegDigit(int x, int y, int w, int h, int digit, Color c) {
    int t = w / 5;
    bool segs[11][7] = {
        {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1}, {0,1,1,0,0,1,1},
        {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}, {0,0,0,0,0,0,1}
    };
    if (digit < 0 || digit > 10) return;
    bool *s = segs[digit];
    if(s[0]) DrawRectangle(x+t, y, w-2*t, t, c);
    if(s[1]) DrawRectangle(x+w-t, y+t, t, h/2-t, c);
    if(s[2]) DrawRectangle(x+w-t, y+h/2, t, h/2-t, c);
    if(s[3]) DrawRectangle(x+t, y+h-t, w-2*t, t, c);
    if(s[4]) DrawRectangle(x, y+h/2, t, h/2-t, c);
    if(s[5]) DrawRectangle(x, y+t, t, h/2-t, c);
    if(s[6]) DrawRectangle(x+t, y+h/2-t/2, w-2*t, t, c);
}

void DrawDistanceVector(int cx, int cy, int height, int dist, Color c) {
    char str[16]; sprintf(str, "%d", dist);
    int len = strlen(str);
    int width = height * 0.6;
    int spacing = width + (height * 0.25);
    int startX = cx - (len * spacing) / 2;
    for(int i=0; i<len; i++) {
        Draw7SegDigit(startX + i*spacing, cy - height/2, width, height, str[i]-'0', c);
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "TW10SP Laser Tester  -  Copyright (c) 2026 Karol \"prz3sp01\" Przespolewski  -  karol@przespol.eu");
    InitAudioDevice();
    SetTextureFilter(GetFontDefault().texture, TEXTURE_FILTER_BILINEAR);
    Sound blipSound = create_laser_blip();

    MUTEX_INIT(g_laser.lock);
    g_laser.port_idx = 1; 
    g_laser.distance_mm = 0; 
    g_laser.connected = false; g_laser.valid_distance = false;
    g_laser.running = true; g_laser.measuring = false; g_laser.show_help = false; g_laser.lang_dropdown_open = false;
    g_laser.lang = LANG_PL; g_laser.sound_on = true; g_laser.log_count = 0; g_laser.total_frames = 0;
    for(int i=0; i<HISTORY_LEN; i++) g_laser.history[i] = 0;

    ThreadHandle tid;
#ifdef _WIN32
    tid = CreateThread(NULL, 0, uart_worker, NULL, 0, NULL);
#else
    pthread_create(&tid, NULL, uart_worker, NULL);
#endif

    SetTargetFPS(60);
    float visual_dist_mm = 300.0f;
    float pulse = 0.0f;
    float frame_timer = 0.0f;
    unsigned int last_total_frames = 0;
    int current_rx_speed = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        pulse += dt * 8.0f;
        frame_timer += dt;
        int sw = GetScreenWidth(); int sh = GetScreenHeight();

        MUTEX_LOCK(g_laser.lock);
        int current_dist = g_laser.distance_mm;
        bool is_connected = g_laser.connected;
        bool is_valid_dist = g_laser.valid_distance;
        bool is_measuring = g_laser.measuring;
        Language lng = g_laser.lang;
        bool play_blip = g_laser.new_sample;
        bool sound_active = g_laser.sound_on;
        bool show_help = g_laser.show_help;
        bool dropdown_open = g_laser.lang_dropdown_open;
        unsigned int current_frames = g_laser.total_frames;
        g_laser.new_sample = false;
        MUTEX_UNLOCK(g_laser.lock);

        if (frame_timer >= 1.0f) {
            current_rx_speed = current_frames - last_total_frames;
            last_total_frames = current_frames;
            frame_timer -= 1.0f;
            if (!is_measuring) current_rx_speed = 0;
        }

        if (play_blip && is_connected && sound_active) PlaySound(blipSound);
        if (is_connected && is_valid_dist) visual_dist_mm += (current_dist - visual_dist_mm) * 0.15f;

        if (IsKeyPressed(KEY_SPACE)) {
            MUTEX_LOCK(g_laser.lock); g_laser.measuring = !g_laser.measuring; MUTEX_UNLOCK(g_laser.lock);
        }

        int topBarH = sh * 0.13;
        int mainY = topBarH + (sh * 0.04);
        int dispH = sh * 0.35;
        int dispW = sw * 0.45;
        int graphX = dispW + (sw * 0.05);
        int graphW = sw - graphX - (sw * 0.03);
        int vizY = mainY + dispH + (sh * 0.04);
        int vizH = sh * 0.20;
        int logY = vizY + vizH + (sh * 0.03);
        int logH = sh - logY;

        Vector2 mouse = GetMousePosition();

        BeginDrawing();
        ClearBackground((Color){ 14, 18, 24, 255 });

        // --- PASEK STEROWANIA (Industrial Header) ---
        DrawRectangle(0, 0, sw, topBarH, (Color){ 20, 26, 34, 255 });
        DrawLine(0, topBarH, sw, topBarH, (Color){ 50, 70, 95, 255 });
        
        // Tytuł w lewym górnym rogu
        DrawText(txt_panel[lng], sw*0.02, topBarH*0.12, topBarH*0.28, (Color){ 220, 235, 255, 255 });
        
        // --- DIODA STANU I RX RATE POD TYTUŁEM (Przesunięte w prawo, brak nakładania) ---
        Color statusColor;
        const char* statusText;
        if (!is_measuring) { statusColor = (Color){ 110, 110, 110, 255 }; statusText = txt_closed[lng]; }
        else if (is_connected && is_valid_dist) { statusColor = (Color){ 46, 204, 113, 255 }; statusText = txt_active[lng]; }
        else if (is_connected && !is_valid_dist) { statusColor = (Color){ 241, 196, 15, 255 }; statusText = txt_no_echo[lng]; } 
        else { statusColor = (Color){ 231, 76, 60, 255 }; statusText = txt_lost[lng]; } 

        DrawCircle(sw*0.02 + 6, topBarH*0.58, topBarH*0.11, statusColor);
        DrawText(statusText, sw*0.02 + 20, topBarH*0.48, topBarH*0.22, statusColor);
        DrawText(TextFormat("%s %d %s", txt_rx[lng], current_rx_speed, txt_fps[lng]), sw*0.02 + 20, topBarH*0.74, topBarH*0.20, (Color){ 110, 140, 175, 255 });

        // --- PRZYCISKI STERUJĄCE Z ETYKIETAMI (LABELS) ---
        int btnW = sw * 0.11; int btnH = topBarH * 0.42; int btnY = topBarH * 0.42;
        int b1 = sw * 0.33;
        float labelSize = topBarH * 0.18;
        
        // 1. Port COM
        DrawText(lbl_port[lng], b1, topBarH * 0.18, labelSize, (Color){ 130, 160, 190, 255 });
        if (DrawCustomButton((Rectangle){b1, btnY, btnW, btnH}, display_ports[g_laser.port_idx], (Color){ 38, 52, 70, 255 }, (Color){ 55, 75, 100, 255 }, btnH*0.4)) {
            MUTEX_LOCK(g_laser.lock); g_laser.port_idx = (g_laser.port_idx + 1) % NUM_PORTS; MUTEX_UNLOCK(g_laser.lock);
        }

        // 2. Buzzer / Audio
        DrawText(lbl_buzzer[lng], b1 + btnW*1.1, topBarH * 0.18, labelSize, (Color){ 130, 160, 190, 255 });
        if (DrawCustomButton((Rectangle){b1 + btnW*1.1, btnY, btnW*0.9, btnH}, sound_active ? "SOUND ON" : "SOUND OFF", sound_active ? (Color){35,105,140,255} : (Color){90,45,45,255}, (Color){50,130,170,255}, btnH*0.4)) {
            MUTEX_LOCK(g_laser.lock); g_laser.sound_on = !g_laser.sound_on; MUTEX_UNLOCK(g_laser.lock);
        }

        // 3. Język / Language
        DrawText(lbl_lang[lng], b1 + btnW*2.15, topBarH * 0.18, labelSize, (Color){ 130, 160, 190, 255 });
        float langDropW = btnW * 1.3f;
        float langDropX = b1 + btnW * 2.15f;
        Rectangle langBox = { langDropX, btnY, langDropW, btnH };
        
        bool langHover = CheckCollisionPointRec(mouse, langBox);
        if (langHover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            MUTEX_LOCK(g_laser.lock); g_laser.lang_dropdown_open = !g_laser.lang_dropdown_open; MUTEX_UNLOCK(g_laser.lock);
        }

        DrawRectangleRounded(langBox, 0.25f, 4, langHover ? (Color){ 55, 75, 100, 255 } : (Color){ 38, 52, 70, 255 });
        DrawRectangleRoundedLines(langBox, 0.25f, 4, (Color){ 100, 130, 160, 120 });
        DrawText(lang_names[lng], langBox.x + 12, langBox.y + (langBox.height - btnH*0.38)/2, btnH*0.38, WHITE);
        DrawText(dropdown_open ? "^" : "v", langBox.x + langBox.width - 20, langBox.y + (langBox.height - btnH*0.38)/2, btnH*0.38, (Color){ 170, 195, 220, 255 });

        // Przycisk Pomocy [ ? ]
        Rectangle helpBtnRect = { langDropX + langDropW + 12, btnY, btnH, btnH };
        if (DrawCustomButton(helpBtnRect, "?", (Color){ 75, 65, 35, 255 }, (Color){ 105, 90, 45, 255 }, btnH * 0.5f)) {
            MUTEX_LOCK(g_laser.lock); g_laser.show_help = !g_laser.show_help; MUTEX_UNLOCK(g_laser.lock);
        }

        // Przycisk Start / Stop Pomiaru
        Color btnStartC = is_measuring ? (Color){ 170, 40, 40, 255 } : (Color){ 35, 130, 65, 255 };
        if (DrawCustomButton((Rectangle){sw - btnW*1.8, btnY, btnW*1.6, btnH}, is_measuring ? txt_stop[lng] : txt_start[lng], btnStartC, Fade(btnStartC, 0.85f), btnH*0.45)) {
            MUTEX_LOCK(g_laser.lock); g_laser.measuring = !g_laser.measuring; MUTEX_UNLOCK(g_laser.lock);
        }

        // --- BLOK 1: ODCZYT CYFROWY (Pro Industrial Display) ---
        int dBoxX = sw * 0.02;
        DrawRectangleRounded((Rectangle){ dBoxX, mainY, dispW, dispH }, 0.04f, 4, (Color){ 10, 14, 20, 255 });
        DrawRectangleRoundedLines((Rectangle){ dBoxX, mainY, dispW, dispH }, 0.04f, 4, (Color){ 45, 65, 90, 200 });

        int numH = dispH * 0.55;
        if (!is_measuring) {
            DrawText(txt_pause[lng], dBoxX + dispW*0.35, mainY + dispH*0.4, numH*0.5, (Color){ 90, 110, 130, 255 });
        } else if (is_connected && is_valid_dist) {
            DrawDistanceVector(dBoxX + dispW*0.4, mainY + dispH*0.5, numH, current_dist, (Color){ 0, 245, 210, 255 });
            DrawText("mm", dBoxX + dispW*0.8, mainY + dispH*0.5, numH*0.3, (Color){ 140, 170, 190, 255 });
            DrawText(TextFormat("%.3f m", current_dist / 1000.0f), dBoxX + dispW*0.74, mainY + dispH*0.18, numH*0.22, (Color){ 100, 220, 150, 255 });
        } else if (is_connected && !is_valid_dist) {
            DrawDistanceVector(dBoxX + dispW*0.4, mainY + dispH*0.5, numH, -1, (Color){ 241, 196, 15, 255 }); 
            DrawText(txt_no_echo[lng], dBoxX + dispW*0.62, mainY + dispH*0.5, numH*0.15, (Color){ 200, 160, 40, 255 });
        } else {
            DrawDistanceVector(dBoxX + dispW*0.4, mainY + dispH*0.5, numH, -1, (Color){ 180, 45, 45, 255 }); 
            DrawText(txt_lost[lng], dBoxX + dispW*0.62, mainY + dispH*0.5, numH*0.15, (Color){ 160, 60, 60, 255 });
        }

        // --- BLOK 2: WYKRES (Trend Chart) ---
        DrawRectangleRounded((Rectangle){ graphX, mainY, graphW, dispH }, 0.04f, 4, (Color){ 10, 14, 20, 255 });
        DrawRectangleRoundedLines((Rectangle){ graphX, mainY, graphW, dispH }, 0.04f, 4, (Color){ 45, 65, 90, 200 });
        
        MUTEX_LOCK(g_laser.lock);
        float graph_max = 5000.0f;
        for(int i=0; i<HISTORY_LEN; i++) if (g_laser.history[i] > graph_max && g_laser.history[i] < 45000) graph_max = g_laser.history[i] + 1000;
        for (int i = 0; i < HISTORY_LEN - 1; i++) {
            int idx = (g_laser.history_head + i) % HISTORY_LEN;
            int nidx = (g_laser.history_head + i + 1) % HISTORY_LEN;
            if (g_laser.history[idx] > 0 && g_laser.history[nidx] > 0) {
                float x1 = graphX + ((float)i / HISTORY_LEN) * graphW;
                float x2 = graphX + ((float)(i+1) / HISTORY_LEN) * graphW;
                float y1 = (mainY + dispH) - ((float)g_laser.history[idx] / graph_max) * dispH;
                float y2 = (mainY + dispH) - ((float)g_laser.history[nidx] / graph_max) * dispH;
                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 2.5f, (Color){ 0, 245, 210, 255 });
            }
        }
        MUTEX_UNLOCK(g_laser.lock);

        // --- WIZUALIZACJA LASERA (Distance Track) ---
        int sensorX = sw * 0.1; int trackMaxW = sw - sensorX - (sw * 0.1);
        int trackY = vizY + vizH*0.3;

        float max_scale_mm = 10000.0f; 
        if (visual_dist_mm > 10000.0f) max_scale_mm = 20000.0f;
        if (visual_dist_mm > 20000.0f) max_scale_mm = 30000.0f;
        if (visual_dist_mm > 30000.0f) max_scale_mm = 40000.0f;

        float ratio = visual_dist_mm / max_scale_mm;
        if (ratio > 1.0f) ratio = 1.0f; if (ratio < 0.03f) ratio = 0.03f;
        int targetX = sensorX + (int)(ratio * trackMaxW);

        DrawLineEx((Vector2){ sensorX, trackY + vizH*0.4 }, (Vector2){ sensorX + trackMaxW, trackY + vizH*0.4 }, 5, (Color){ 45, 65, 90, 255 });
        DrawText(TextFormat("SCALE: 0 - %.0f m (MAX 40 m)", max_scale_mm/1000), sensorX + 15, trackY + vizH*0.58, vizH*0.14, (Color){ 100, 130, 160, 255 });

        int step = max_scale_mm / 10;
        for (int m = 0; m <= max_scale_mm; m += step) {
            float tickR = (float)m / max_scale_mm;
            int tickX = sensorX + (int)(tickR * trackMaxW);
            DrawLineEx((Vector2){ tickX, trackY + vizH*0.3 }, (Vector2){ tickX, trackY + vizH*0.5 }, 3, (Color){ 75, 100, 130, 255 });
            DrawText(TextFormat("%.0fm", (float)m/1000), tickX - vizH*0.1, trackY + vizH*0.68, vizH*0.14, (Color){ 100, 130, 160, 255 });
        }

        int modSize = vizH * 0.8;
        DrawRectangleRounded((Rectangle){ sensorX - modSize, trackY - modSize*0.5, modSize, modSize }, 0.1f, 4, (Color){ 25, 35, 50, 255 });
        DrawRectangleLines((sensorX - modSize), trackY - modSize*0.5, modSize, modSize, (Color){ 65, 90, 125, 255 });
        
        if (is_connected && is_valid_dist && is_measuring) {
            float glowAlpha = 0.2f + 0.15f * sinf(pulse);
            DrawLineEx((Vector2){ sensorX, trackY }, (Vector2){ targetX, trackY }, 16, Fade((Color){ 255, 30, 30, 255 }, glowAlpha * 0.5f));
            DrawLineEx((Vector2){ sensorX, trackY }, (Vector2){ targetX, trackY }, 5, (Color){ 255, 160, 160, 255 });
            DrawCircle(targetX, trackY, 10 + 3.0f * sinf(pulse), (Color){ 255, 40, 40, 255 });
        }
        
        DrawRectangleRounded((Rectangle){ targetX, trackY - modSize*0.6, modSize*0.3, modSize*1.2 }, 0.1f, 4, (Color){ 140, 160, 180, 255 });
        DrawRectangleLines(targetX, trackY - modSize*0.6, modSize*0.3, modSize*1.2, (Color){ 200, 220, 240, 255 });

        // --- TERMINAL LOGÓW ---
        DrawRectangle(0, logY, sw, logH, (Color){ 10, 14, 20, 255 });
        DrawLine(0, logY, sw, logY, (Color){ 50, 70, 95, 255 });
        DrawText(TextFormat("> %s", txt_logs[lng]), sw*0.02, logY + logH*0.1, logH*0.12, (Color){ 140, 170, 190, 255 });

        MUTEX_LOCK(g_laser.lock);
        for (int i = 0; i < g_laser.log_count; i++) {
            Color txtCol = (Color){ 170, 195, 215, 255 };
            if (strstr(g_laser.logs[i], "BLAD") || strstr(g_laser.logs[i], "FOUT") || strstr(g_laser.logs[i], "ERROR")) txtCol = (Color){ 230, 80, 80, 255 };
            if (strstr(g_laser.logs[i], "SYSTEM")) txtCol = (Color){ 100, 200, 140, 255 };
            DrawText(g_laser.logs[i], sw*0.02, logY + logH*0.3 + (i * logH*0.11), logH*0.10, txtCol);
        }
        MUTEX_UNLOCK(g_laser.lock);

        // =========================================================================
        // WARSTWA WIERZCHNIA (DROPDOWN I MODAL POMOCY)
        // =========================================================================

        if (dropdown_open) {
            for (int i = 0; i < 3; i++) {
                Rectangle optBox = { langDropX, btnY + btnH + 4 + (i * btnH), langDropW, btnH };
                bool optHover = CheckCollisionPointRec(mouse, optBox);
                
                if (optHover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    MUTEX_LOCK(g_laser.lock);
                    g_laser.lang = (Language)i;
                    g_laser.lang_dropdown_open = false;
                    MUTEX_UNLOCK(g_laser.lock);
                    clear_logs_safe();
                }

                DrawRectangleRec(optBox, optHover ? (Color){ 55, 80, 115, 255 } : (Color){ 26, 36, 50, 255 });
                DrawRectangleLinesEx(optBox, 1, (Color){ 80, 110, 140, 180 });
                DrawText(lang_names[i], optBox.x + 12, optBox.y + (optBox.height - btnH*0.38)/2, btnH*0.38, (i == lng) ? (Color){ 0, 245, 210, 255 } : WHITE);
            }
            
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !CheckCollisionPointRec(mouse, (Rectangle){ langDropX, btnY, langDropW, btnH * 4 })) {
                MUTEX_LOCK(g_laser.lock); g_laser.lang_dropdown_open = false; MUTEX_UNLOCK(g_laser.lock);
            }
        }

        if (show_help) {
            DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 190 });
            
            int pW = sw * 0.65; int pH = sh * 0.65;
            int pX = (sw - pW) / 2; int pY = (sh - pH) / 2;
            
            DrawRectangleRounded((Rectangle){ pX, pY, pW, pH }, 0.03f, 4, (Color){ 18, 24, 32, 255 });
            DrawRectangleRoundedLines((Rectangle){ pX, pY, pW, pH }, 0.03f, 4, (Color){ 0, 245, 210, 255 });
            
            DrawText(txt_help_title[lng], pX + pW*0.05, pY + pH*0.06, pH*0.055, (Color){ 0, 245, 210, 255 });
            DrawLine(pX + pW*0.05, pY + pH*0.14, pX + pW*0.95, pY + pH*0.14, (Color){ 45, 65, 90, 255 });
            
            DrawText(txt_help_body[lng], pX + pW*0.05, pY + pH*0.18, pH*0.033, (Color){ 200, 220, 235, 255 });
            
            Rectangle closeHelpBtn = { pX + pW*0.35, pY + pH*0.82, pW*0.30, pH*0.12 };
            if (DrawCustomButton(closeHelpBtn, txt_close_btn[lng], (Color){ 170, 45, 45, 255 }, (Color){ 200, 65, 65, 255 }, pH*0.045)) {
                MUTEX_LOCK(g_laser.lock); g_laser.show_help = false; MUTEX_UNLOCK(g_laser.lock);
            }
        }

        EndDrawing();
    }

    g_laser.running = false;
#ifdef _WIN32
    WaitForSingleObject(tid, INFINITE);
#else
    pthread_join(tid, NULL);
#endif
    UnloadSound(blipSound); CloseAudioDevice(); CloseWindow();
    return 0;
}