/*******************************************************************************************
 *
 * TW10SP Laser Tester v1.1
 *
 * Description:
 * Advanced desktop monitoring, diagnostic, and control utility for the Mileseey TW10SP
 * (Firmware Li3, Soft v30) laser distance sensor.
 * Communicates via UART at 9600 8N1 using pure ASCII commands (no CR/LF terminators).
 * Built with Raylib.
 *
 * Author: Karol "prz3sp01" Przespolewski (karol@przespol.eu)
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
    #define MUTEX_INIT(m) do { \
        pthread_mutexattr_t attr; \
        pthread_mutexattr_init(&attr); \
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); \
        pthread_mutex_init(&m, &attr); \
        pthread_mutexattr_destroy(&attr); \
    } while(0)
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
#define MAX_LOGS 6
#define HISTORY_LEN 150
#define MAX_CMD_QUEUE 16
#define MODULE_BASE_OFFSET_MM 66 // Fizyczna długość modułu TW10SP (Front vs Back)

typedef enum { LANG_PL = 0, LANG_NL, LANG_EN } Language;

// Etykiety przycisków
const char* txt_start[]    = { "CIAGLY", "CONTINU", "CONTINUOUS" };
const char* txt_stop[]     = { "ZATRZYMAJ", "STOPPEN", "STOP" };
const char* txt_single[]   = { "POJEDYNCZY", "ENKELE METING", "SINGLE SHOT" };
const char* txt_snd_on[]   = { "DZWIEK WL", "GELUID AAN", "AUDIO ON" };
const char* txt_snd_off[]  = { "DZWIEK WYL", "GELUID UIT", "AUDIO OFF" };
const char* txt_laser_on[] = { "LASER WL", "LASER AAN", "LASER ON" };
const char* txt_laser_off[]= { "LASER WYL", "LASER UIT", "LASER OFF" };
const char* txt_base_f[]   = { "BAZA: PRZOD", "BASIS: VOOR", "BASE: FRONT" };
const char* txt_base_b[]   = { "BAZA: TYL", "BASIS: ACHTER", "BASE: BACK" };

// Statusy systemowe
const char* txt_active[]   = { "POMIAR CIAGLY", "CONTINUE METING", "STREAMING" };
const char* txt_starting[] = { "URUCHAMIANIE...", "OPSTARTEN...", "STARTING..." };
const char* txt_timeout[]  = { "BRAK ODPOWIEDZI", "GEEN REACTIE", "NO RESPONSE" };
const char* txt_standby[]  = { "GOTOWY (STANDBY)", "GEREED (STANDBY)", "STANDBY" };
const char* txt_lost[]     = { "BRAK POLACZENIA", "GEEN VERBINDING", "NO CONNECTION" };
const char* txt_logs[]     = { "DZIENNIK TELEMETRII I ZDARZEN", "TELEMETRIE- EN GEBEURTENISSENLOG", "TELEMETRY & SYSTEM LOG" };
const char* txt_rx[]       = { "RX:", "RX:", "RX:" };
const char* txt_fps[]      = { "ramki/s", "fps", "fps" };
const char* lang_names[]   = { "Polski (PL)", "Nederlands (NL)", "English (EN)" };

const char* txt_help_title[] = { 
    "SPECYFIKACJA & KOMENDY TW10SP (Li3 / v30)", 
    "SPECIFICATIE & COMMANDO'S TW10SP (Li3 / v30)", 
    "TW10SP PROTOCOL SPECIFICATION (Li3 / v30)" 
};
const char* txt_help_body[] = {
    "Obslugiwany modul: Mileseey Li3 / TW10SP (PCB:11, Soft:30)\n"
    "Parametry UART: 9600 Baud, 8N1, poziom logiczny TTL 3.3V\n\n"
    "KOMENDY STERUJACE (Czysty ASCII bez CR/LF):\n"
    "- contis / stopcontis : Start i stop szybkiego strumienia (6 Hz)\n"
    "- single             : Wyzwolenie pojedynczego pomiaru odleglosci\n"
    "- setlaser 1 / 0     : Wlaczenie lub wylaczenie celownika laserowego\n"
    "- withsignal 1       : Aktywacja rozszerzonej ramki telemetrycznej (S, I, N, E)\n\n"
    "BAZA POMIAROWA (OFFSET):\n"
    "Przelacznik Przod/Tyl realizowany jest bezstratnie programowo (+66 mm),\n"
    "co eliminuje zrywanie transmisji i zapewnia stabilne 6 Hz bez zwiech.\n\n"
    "KODY DIAGNOSTYCZNE E:\n"
    "E:0 (OK), E:15 (Slaby sygnal/brak celu), E:16 (Przeswietlenie APD),\n"
    "E:23 (Poza zakresem <5cm lub >40m), E:25 (Zbyt niskie VCC), E:26 (Przegrzanie).",

    "Ondersteunde module: Mileseey Li3 / TW10SP (PCB:11, Soft:30)\n"
    "UART-parameters: 9600 Baud, 8N1, logisch niveau TTL 3.3V\n\n"
    "BESTURINGSCOMMANDO'S (Zuivere ASCII zonder CR/LF):\n"
    "- contis / stopcontis : Start en stop van continue stroom (6 Hz)\n"
    "- single             : Enkele precisiemeting uitvoeren\n"
    "- setlaser 1 / 0     : Laservizier in- of uitschakelen\n"
    "- withsignal 1       : Uitgebreide telemetrie inschakelen (S, I, N, E)\n\n"
    "MEETBASIS (OFFSET):\n"
    "Schakelen Voor/Achter gebeurt via software-offset (+66 mm) zonder\n"
    "de UART-stroom te onderbreken voor maximale stabiliteit.\n\n"
    "FOUTCODES E:\n"
    "E:0 (OK), E:15 (Signaal te zwak), E:16 (Signaal te sterk),\n"
    "E:23 (Buiten bereik), E:25 (Lage spanning VCC), E:26 (Oververhitting).",

    "Supported Module: Mileseey Li3 / TW10SP (PCB:11, Soft:30)\n"
    "UART Interface: 9600 Baud, 8N1, 3.3V TTL Logic Level\n\n"
    "CONTROL COMMANDS (Pure ASCII without CR/LF terminators):\n"
    "- contis / stopcontis : Start and stop continuous measurement (6 Hz)\n"
    "- single             : Trigger single high-precision measurement\n"
    "- setlaser 1 / 0     : Toggle aiming laser diode on/off\n"
    "- withsignal 1       : Enable full telemetry stream (S, I, N, E)\n\n"
    "MEASUREMENT BASE (OFFSET):\n"
    "Front/Rear base switching is handled via seamless software offset (+66 mm)\n"
    "preventing stream interrupts and hardware EEPROM freeze.\n\n"
    "DIAGNOSTIC ERROR CODES E:\n"
    "E:0 (OK), E:15 (Weak signal / no target), E:16 (Sensor saturated),\n"
    "E:23 (Out of range <5cm or >40m), E:25 (Low VCC), E:26 (Over-temperature)."
};
const char* txt_close_btn[] = { "ZAMKNIJ", "SLUITEN", "CLOSE" };

const char* get_error_desc(int err, Language lang) {
    switch (err) {
        case 0:
            return (lang == LANG_PL) ? "POMIAR PRAWIDLOWY (OK)" : (lang == LANG_NL) ? "METING GELDIG (OK)" : "MEASUREMENT VALID (OK)";
        case 15:
            return (lang == LANG_PL) ? "ZBYT SLABY SYGNAL / BRAK CELU" : (lang == LANG_NL) ? "SIGNAAL TE ZWAK / GEEN DOEL" : "SIGNAL TOO WEAK / NO TARGET";
        case 16:
            return (lang == LANG_PL) ? "PRZESWIETLENIE ODBIORNIKA APD" : (lang == LANG_NL) ? "SIGNAAL TE STERK (VERBLIND)" : "SIGNAL TOO STRONG / SATURATED";
        case 23:
            return (lang == LANG_PL) ? "POZA ZAKRESEM (<0.05m LUB >40m)" : (lang == LANG_NL) ? "BUITEN BEREIK (<0.05m OF >40m)" : "OUT OF RANGE (<0.05m OR >40m)";
        case 24:
            return (lang == LANG_PL) ? "BLAD OBLICZEN FAZY OPTYKI" : (lang == LANG_NL) ? "FASEBEREKENINGSFOUT" : "PHASE CALCULATION ERROR";
        case 25:
            return (lang == LANG_PL) ? "ZBYT NISKIE ZASILANIE VCC" : (lang == LANG_NL) ? "TE LAGE VOEDINGSSPANNING VCC" : "LOW SUPPLY VOLTAGE (VCC)";
        case 26:
            return (lang == LANG_PL) ? "PRZEGRZANIE STRUKTURY MODULU" : (lang == LANG_NL) ? "MODULE OVERVERHIT" : "OVER-TEMPERATURE DETECTED";
        case 255:
            return (lang == LANG_PL) ? "TIMEOUT ODBIORNIKA / BRAK ECHA" : (lang == LANG_NL) ? "ONTVANGER TIME-OUT" : "HARDWARE TIMEOUT / NO ECHO";
        default:
            return (lang == LANG_PL) ? "NIEZNANY BLAD POMIARU" : (lang == LANG_NL) ? "ONBEKENDE FOUT" : "UNKNOWN ERROR CODE";
    }
}

typedef struct {
    char cmd[32];
    int delay_after_ms;
} CommandItem;

typedef struct {
    int port_idx;
    int raw_distance_mm;  // Surowy odczyt z czujnika (Front lens)
    int distance_mm;      // Odczyt z uwzględnieniem wybranej bazy
    int signal_mv;
    int noise_mv;
    int internal_mv;
    int error_code;
    int last_logged_error;

    bool connected;        
    bool valid_distance;   
    bool new_sample;
    bool running;
    bool measuring_continuous;
    bool laser_active;
    bool base_front;

    long long last_rx_time_ms;
    long long start_measure_time_ms;

    Language lang;
    bool sound_on;
    bool show_help;
    bool lang_dropdown_open;
    unsigned int total_frames;

    CommandItem cmd_queue[MAX_CMD_QUEUE];
    int cmd_count;

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

void queue_command_ex(const char* cmd, int delay_after_ms) {
    MUTEX_LOCK(g_laser.lock);
    if (g_laser.cmd_count < MAX_CMD_QUEUE) {
        strncpy(g_laser.cmd_queue[g_laser.cmd_count].cmd, cmd, 31);
        g_laser.cmd_queue[g_laser.cmd_count].cmd[31] = '\0';
        g_laser.cmd_queue[g_laser.cmd_count].delay_after_ms = delay_after_ms;
        g_laser.cmd_count++;
    }
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
    char rx_buffer[256];
    int rx_idx = 0;

    while (g_laser.running) {
        int target_port_idx; Language lng;
        MUTEX_LOCK(g_laser.lock);
        target_port_idx = g_laser.port_idx;
        lng = g_laser.lang;
        MUTEX_UNLOCK(g_laser.lock);

        if (current_port_idx != target_port_idx) {
            if (fd != INVALID_SERIAL_HANDLE) { 
                serial_write(fd, "stopcontis", 10);
                SLEEP_MS(50);
                serial_close(fd); 
                fd = INVALID_SERIAL_HANDLE; 
            }
            current_port_idx = target_port_idx;
        }

        if (fd == INVALID_SERIAL_HANDLE) {
            fd = serial_open(available_ports[current_port_idx]);
            if (fd == INVALID_SERIAL_HANDLE) {
                MUTEX_LOCK(g_laser.lock); g_laser.connected = false; MUTEX_UNLOCK(g_laser.lock);
                SLEEP_MS(500);
                continue;
            } else {
                MUTEX_LOCK(g_laser.lock); g_laser.connected = true; MUTEX_UNLOCK(g_laser.lock);
                add_log(TextFormat("[PORT] OK -> %s (9600 8N1)", display_ports[current_port_idx]));
                SLEEP_MS(100);
                serial_write(fd, "stopcontis", 10);
                SLEEP_MS(150);
                // Gwarantujemy bazę sprzętową na czoło modułu (Front)
                serial_write(fd, "setbase 1", 9);
                SLEEP_MS(150);
                serial_write(fd, "withsignal 1", 12);
                SLEEP_MS(120);
#ifdef _WIN32
                PurgeComm(fd, PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
                tcflush(fd, TCIOFLUSH);
#endif
                rx_idx = 0;
            }
        }

        // Realizacja poleceń z kolejki FIFO
        char cmd_to_send[32] = { 0 };
        int cmd_delay = 50;
        bool has_cmd = false;

        MUTEX_LOCK(g_laser.lock);
        if (g_laser.cmd_count > 0) {
            strcpy(cmd_to_send, g_laser.cmd_queue[0].cmd);
            cmd_delay = g_laser.cmd_queue[0].delay_after_ms;
            for (int i = 1; i < g_laser.cmd_count; i++) {
                g_laser.cmd_queue[i-1] = g_laser.cmd_queue[i];
            }
            g_laser.cmd_count--;
            has_cmd = true;
        }
        MUTEX_UNLOCK(g_laser.lock);

        if (has_cmd && fd != INVALID_SERIAL_HANDLE) {
            serial_write(fd, cmd_to_send, strlen(cmd_to_send));
            add_log(TextFormat("[TX] %s", cmd_to_send));
            SLEEP_MS(cmd_delay);

            if (strstr(cmd_to_send, "stop") != NULL) {
#ifdef _WIN32
                PurgeComm(fd, PURGE_RXCLEAR);
#else
                tcflush(fd, TCIFLUSH);
#endif
                rx_idx = 0;
            }
        }

        // Odbiór danych UART
        char temp[64];
        int r = serial_read(fd, temp, sizeof(temp));
        if (r > 0) {
            for (int i = 0; i < r; i++) {
                if (temp[i] == '\n') {
                    rx_buffer[rx_idx] = '\0';
                    
                    float dist_m = -1.0f;
                    int sig = 0, internal_v = 0, noise = 0, err = 0;
                    bool parsed_any = false;

                    char* pD = strstr(rx_buffer, "D:");
                    char* pS = strstr(rx_buffer, "S:");
                    char* pI = strstr(rx_buffer, "I:");
                    char* pN = strstr(rx_buffer, "N:");
                    char* pE = strstr(rx_buffer, "E:");

                    if (pE && sscanf(pE, "E:%d", &err) == 1) parsed_any = true;
                    if (pS && sscanf(pS, "S:%dmV", &sig) == 1) parsed_any = true;
                    if (pN && sscanf(pN, "N:%dmV", &noise) == 1) parsed_any = true;
                    if (pI && sscanf(pI, "I:%dmV", &internal_v) == 1) parsed_any = true;
                    if (pD && sscanf(pD, "D:%fm", &dist_m) == 1) parsed_any = true;

                    if (parsed_any) {
                        int log_err = -1;
                        MUTEX_LOCK(g_laser.lock);
                        g_laser.connected = true;
                        g_laser.total_frames++;
                        g_laser.signal_mv = sig;
                        g_laser.noise_mv = noise;
                        g_laser.internal_mv = internal_v;
                        g_laser.error_code = err;
                        g_laser.last_rx_time_ms = get_time_ms();

                        if (err == 0 && dist_m > 0.04f && dist_m < 45.0f) {
                            int raw_mm = (int)(dist_m * 1000.0f);
                            g_laser.raw_distance_mm = raw_mm;
                            // Automatyczne dodanie stałego offsetu modułu dla bazy tylnej
                            int final_mm = raw_mm + (g_laser.base_front ? 0 : MODULE_BASE_OFFSET_MM);

                            g_laser.distance_mm = final_mm;
                            g_laser.valid_distance = true;
                            g_laser.new_sample = true;
                            g_laser.history[g_laser.history_head] = final_mm;
                            g_laser.history_head = (g_laser.history_head + 1) % HISTORY_LEN;
                        } else {
                            g_laser.valid_distance = false;
                        }

                        if (err != g_laser.last_logged_error) {
                            log_err = err;
                            g_laser.last_logged_error = err;
                        }
                        MUTEX_UNLOCK(g_laser.lock);

                        if (log_err != -1) {
                            if (log_err != 0) {
                                add_log(TextFormat("[ERROR] E:%d -> %s", log_err, get_error_desc(log_err, lng)));
                            } else {
                                add_log(TextFormat("[STATUS] E:0 -> %s", get_error_desc(0, lng)));
                            }
                        }
                    }
                    rx_idx = 0;
                } else if (temp[i] != '\r') {
                    if (rx_idx < (int)sizeof(rx_buffer) - 1) rx_buffer[rx_idx++] = temp[i];
                }
            }
        } else {
            SLEEP_MS(3);
        }

        MUTEX_LOCK(g_laser.lock);
        if (get_time_ms() - g_laser.last_rx_time_ms > 3000) {
            g_laser.valid_distance = false;
        }
        MUTEX_UNLOCK(g_laser.lock);
    }

    if (fd != INVALID_SERIAL_HANDLE) {
        serial_write(fd, "stopcontis", 10);
        SLEEP_MS(50);
        serial_close(fd);
    }
    return 0;
}

Sound create_laser_blip(void) {
    Wave wave = { 0 };
    wave.frameCount = SAMPLE_RATE / 14;
    wave.sampleRate = SAMPLE_RATE;
    wave.sampleSize = 16;
    wave.channels = 1;

    short *samples = (short*)malloc(wave.frameCount * sizeof(short));
    if (samples == NULL) return (Sound){ 0 };

    for (int i = 0; i < (int)wave.frameCount; i++) {
        float t = (float)i / SAMPLE_RATE;
        float freq = 1750.0f - 850.0f * (t / 0.07f);
        samples[i] = (short)(sinf(2.0f * PI * freq * t) * (1.0f - (t / 0.07f)) * 11000.0f);
    }

    wave.data = samples;
    Sound snd = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return snd;
}

bool DrawCustomButton(Rectangle rect, const char* text, Color colorMain, Color colorHover, int fontSize) {
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, rect);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    DrawRectangleRounded(rect, 0.22f, 4, hover ? colorHover : colorMain);
    DrawRectangleRoundedLines(rect, 0.22f, 4, (Color){110, 140, 175, 140});
    int textW = MeasureText(text, fontSize);
    DrawText(text, rect.x + (rect.width - textW)/2, rect.y + (rect.height - fontSize)/2, fontSize, WHITE);
    return clicked;
}

void Draw7SegChar(int x, int y, int w, int h, char c, Color col) {
    int t = w / 5;
    bool s[7] = {0};
    if (c >= '0' && c <= '9') {
        bool segs[10][7] = {
            {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1}, {0,1,1,0,0,1,1},
            {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}
        };
        for(int i=0; i<7; i++) s[i] = segs[c - '0'][i];
    } else if (c == '-') {
        s[6] = 1;
    } else if (c == 'E' || c == 'e') {
        s[0] = 1; s[5] = 1; s[6] = 1; s[4] = 1; s[3] = 1;
    }
    if(s[0]) DrawRectangle(x+t, y, w-2*t, t, col);
    if(s[1]) DrawRectangle(x+w-t, y+t, t, h/2-t, col);
    if(s[2]) DrawRectangle(x+w-t, y+h/2, t, h/2-t, col);
    if(s[3]) DrawRectangle(x+t, y+h-t, w-2*t, t, col);
    if(s[4]) DrawRectangle(x, y+h/2, t, h/2-t, col);
    if(s[5]) DrawRectangle(x, y+t, t, h/2-t, col);
    if(s[6]) DrawRectangle(x+t, y+h/2-t/2, w-2*t, t, col);
}

void Draw7SegString(int cx, int cy, int height, const char* str, Color c) {
    int len = strlen(str);
    int width = height * 0.58f;
    int spacing = width + (height * 0.22f);
    int startX = cx - (len * spacing) / 2;
    for(int i=0; i<len; i++) {
        Draw7SegChar(startX + i*spacing, cy - height/2, width, height, str[i], c);
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED | FLAG_MSAA_4X_HINT);
    InitWindow(1360, 768, "TW10SP Laser Tester v1.1  -  Copyright (c) 2026 Karol \"prz3sp01\" Przespolewski  -  karol@przespol.eu");
    InitAudioDevice();
    SetTextureFilter(GetFontDefault().texture, TEXTURE_FILTER_BILINEAR);
    Sound blipSound = create_laser_blip();

    MUTEX_INIT(g_laser.lock);
    g_laser.port_idx = 1; 
    g_laser.raw_distance_mm = 0;
    g_laser.distance_mm = 0; 
    g_laser.signal_mv = 0;
    g_laser.noise_mv = 0;
    g_laser.internal_mv = 0;
    g_laser.error_code = 0;
    g_laser.last_logged_error = 0;
    g_laser.connected = false; 
    g_laser.valid_distance = false;
    g_laser.running = true; 
    g_laser.measuring_continuous = false; 
    g_laser.laser_active = false;
    g_laser.base_front = true;
    g_laser.show_help = false; 
    g_laser.lang_dropdown_open = false;
    g_laser.lang = LANG_PL; 
    g_laser.sound_on = true; 
    g_laser.log_count = 0; 
    g_laser.cmd_count = 0;
    g_laser.total_frames = 0;
    g_laser.last_rx_time_ms = 0;
    g_laser.start_measure_time_ms = 0;
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
        int sig_mv = g_laser.signal_mv;
        int noise_mv = g_laser.noise_mv;
        int err_code = g_laser.error_code;
        bool is_connected = g_laser.connected;
        bool is_valid_dist = g_laser.valid_distance;
        bool is_measuring = g_laser.measuring_continuous;
        bool laser_on = g_laser.laser_active;
        bool base_fr = g_laser.base_front;
        Language lng = g_laser.lang;
        bool play_blip = g_laser.new_sample;
        bool sound_active = g_laser.sound_on;
        bool show_help = g_laser.show_help;
        bool dropdown_open = g_laser.lang_dropdown_open;
        unsigned int current_frames = g_laser.total_frames;
        long long last_rx_ms = g_laser.last_rx_time_ms;
        long long start_meas_ms = g_laser.start_measure_time_ms;
        g_laser.new_sample = false;
        MUTEX_UNLOCK(g_laser.lock);

        if (frame_timer >= 1.0f) {
            current_rx_speed = current_frames - last_total_frames;
            last_total_frames = current_frames;
            frame_timer -= 1.0f;
        }

        if (play_blip && is_connected && sound_active && err_code == 0) PlaySound(blipSound);
        if (is_connected && is_valid_dist) visual_dist_mm += (current_dist - visual_dist_mm) * 0.20f;

        int topBarH = sh * 0.14f;
        int mainY = topBarH + (sh * 0.03f);
        int dispH = sh * 0.38f;
        int dispW = sw * 0.46f;
        int graphX = dispW + (sw * 0.04f);
        int graphW = sw - graphX - (sw * 0.02f);
        int vizY = mainY + dispH + (sh * 0.03f);
        int vizH = sh * 0.19f;
        int logY = vizY + vizH + (sh * 0.025f);
        int logH = sh - logY;

        Vector2 mouse = GetMousePosition();

        BeginDrawing();
        ClearBackground((Color){ 13, 17, 23, 255 });

        // =========================================================================
        // 1. GŁÓWNY PASEK STEROWANIA I PRZYCISKÓW
        // =========================================================================
        DrawRectangle(0, 0, sw, topBarH, (Color){ 20, 26, 36, 255 });
        DrawLine(0, topBarH, sw, topBarH, (Color){ 50, 72, 98, 255 });
        
        Color statusColor = (Color){ 120, 120, 120, 255 };
        const char* statusText = txt_standby[lng];
        long long now_ms = get_time_ms();

        if (!is_connected) {
            statusColor = (Color){ 230, 75, 60, 255 };
            statusText = txt_lost[lng];
        } else if (err_code != 0) {
            statusColor = (Color){ 245, 166, 35, 255 };
            statusText = TextFormat("ERROR E:%d", err_code);
        } else if (is_measuring) {
            if (current_rx_speed > 0 || (now_ms - last_rx_ms < 1200)) {
                statusColor = (Color){ 46, 204, 113, 255 };
                statusText = txt_active[lng];
            } else if (now_ms - start_meas_ms < 1500) {
                statusColor = (Color){ 245, 200, 50, 255 };
                statusText = txt_starting[lng];
            } else {
                statusColor = (Color){ 230, 75, 60, 255 };
                statusText = txt_timeout[lng];
            }
        }

        // Dioda statusu z radialną poświatą
        float ledCenterX = sw * 0.025f;
        float ledCenterY = topBarH * 0.44f;
        float ledCoreR   = topBarH * 0.12f;

        if (is_connected && is_measuring && err_code == 0) {
            float aura1 = ledCoreR * (1.35f + 0.30f * sinf(pulse));
            float aura2 = ledCoreR * (1.75f + 0.45f * sinf(pulse * 0.8f));
            DrawCircle(ledCenterX, ledCenterY, aura2, Fade(statusColor, 0.15f));
            DrawCircle(ledCenterX, ledCenterY, aura1, Fade(statusColor, 0.35f));
        } else if (err_code != 0 || !is_connected) {
            float blinkAlpha = 0.35f + 0.35f * sinf(pulse * 1.5f);
            DrawCircle(ledCenterX, ledCenterY, ledCoreR * 1.4f, Fade(statusColor, blinkAlpha));
        }
        
        DrawCircle(ledCenterX, ledCenterY, ledCoreR, statusColor);
        DrawCircleLines(ledCenterX, ledCenterY, ledCoreR + 2, (Color){ 220, 240, 255, 180 });
        DrawCircle(ledCenterX - ledCoreR * 0.28f, ledCenterY - ledCoreR * 0.28f, ledCoreR * 0.32f, (Color){ 255, 255, 255, 180 });

        float textOffsetX = ledCenterX + ledCoreR + 18.0f;
        DrawText(statusText, textOffsetX, topBarH * 0.26f, topBarH * 0.27f, statusColor);
        DrawText(TextFormat("%s %d %s", txt_rx[lng], current_rx_speed, txt_fps[lng]), 
                 textOffsetX, topBarH * 0.60f, topBarH * 0.18f, (Color){ 120, 150, 185, 255 });

        // PRZYCISKI AKCJI
        int btnY = topBarH * 0.26f;
        int btnH = topBarH * 0.52f;
        float btnW = sw * 0.082f;
        float startX = sw * 0.28f;
        int bGap = sw * 0.006f;

        // B1: Wybór portu
        if (DrawCustomButton((Rectangle){ startX, btnY, btnW, btnH }, display_ports[g_laser.port_idx], (Color){ 36, 48, 65, 255 }, (Color){ 55, 75, 105, 255 }, btnH * 0.36f)) {
            MUTEX_LOCK(g_laser.lock); g_laser.port_idx = (g_laser.port_idx + 1) % NUM_PORTS; MUTEX_UNLOCK(g_laser.lock);
        }

        // B2: Dźwięk
        startX += btnW + bGap;
        const char* sndText = sound_active ? txt_snd_on[lng] : txt_snd_off[lng];
        if (DrawCustomButton((Rectangle){ startX, btnY, btnW*1.05f, btnH }, sndText, 
                             sound_active ? (Color){ 30, 95, 130, 255 } : (Color){ 85, 45, 45, 255 }, 
                             (Color){ 45, 120, 160, 255 }, btnH * 0.35f)) {
            MUTEX_LOCK(g_laser.lock); g_laser.sound_on = !g_laser.sound_on; MUTEX_UNLOCK(g_laser.lock);
        }

        // B3: Baza pomiarowa Front/Back (Natychmiastowe, bezstratne przełączanie programowe)
        startX += btnW*1.05f + bGap;
        if (DrawCustomButton((Rectangle){ startX, btnY, btnW*1.05f, btnH }, base_fr ? txt_base_f[lng] : txt_base_b[lng], (Color){ 40, 65, 90, 255 }, (Color){ 60, 95, 130, 255 }, btnH * 0.34f)) {
            MUTEX_LOCK(g_laser.lock);
            g_laser.base_front = !g_laser.base_front;
            bool bf = g_laser.base_front;
            if (g_laser.raw_distance_mm > 0) {
                g_laser.distance_mm = g_laser.raw_distance_mm + (bf ? 0 : MODULE_BASE_OFFSET_MM);
            }
            MUTEX_UNLOCK(g_laser.lock);
            add_log(TextFormat("[BASE] %s (%s)", bf ? "FRONT" : "BACK", bf ? "0 mm" : "+66 mm"));
        }

        // B4: Celownik laserowy ON/OFF
        startX += btnW*1.05f + bGap;
        if (DrawCustomButton((Rectangle){ startX, btnY, btnW*1.05f, btnH }, laser_on ? txt_laser_on[lng] : txt_laser_off[lng], laser_on ? (Color){ 175, 45, 45, 255 } : (Color){ 45, 55, 70, 255 }, (Color){ 205, 65, 65, 255 }, btnH * 0.35f)) {
            bool was_measuring = false;
            MUTEX_LOCK(g_laser.lock);
            if (g_laser.measuring_continuous) {
                g_laser.measuring_continuous = false;
                g_laser.laser_active = false;
                was_measuring = true;
            } else {
                g_laser.laser_active = !g_laser.laser_active;
            }
            bool lstate = g_laser.laser_active;
            MUTEX_UNLOCK(g_laser.lock);

            if (was_measuring) {
                queue_command_ex("stopcontis", 150);
                queue_command_ex("setlaser 0", 80);
            } else {
                queue_command_ex(lstate ? "setlaser 1" : "setlaser 0", 80);
            }
        }

        // B5: Pojedynczy pomiar
        startX += btnW*1.05f + bGap;
        if (DrawCustomButton((Rectangle){ startX, btnY, btnW*1.15f, btnH }, txt_single[lng], (Color){ 120, 85, 25, 255 }, (Color){ 160, 115, 35, 255 }, btnH * 0.36f)) {
            bool was_measuring = false;
            MUTEX_LOCK(g_laser.lock);
            if (g_laser.measuring_continuous) {
                g_laser.measuring_continuous = false;
                was_measuring = true;
            }
            g_laser.laser_active = false;
            MUTEX_UNLOCK(g_laser.lock);

            if (was_measuring) {
                queue_command_ex("stopcontis", 150);
            }
            queue_command_ex("single", 100);
        }

        // B6: Pomiar ciągły START/STOP
        startX += btnW*1.15f + bGap;
        Color btnContColor = is_measuring ? (Color){ 175, 40, 40, 255 } : (Color){ 35, 135, 70, 255 };
        if (DrawCustomButton((Rectangle){ startX, btnY, btnW*1.25f, btnH }, is_measuring ? txt_stop[lng] : txt_start[lng], btnContColor, Fade(btnContColor, 0.85f), btnH * 0.38f)) {
            MUTEX_LOCK(g_laser.lock);
            g_laser.measuring_continuous = !g_laser.measuring_continuous;
            bool mc = g_laser.measuring_continuous;
            g_laser.laser_active = mc;
            if (mc) {
                g_laser.start_measure_time_ms = get_time_ms();
            }
            MUTEX_UNLOCK(g_laser.lock);

            if (mc) {
                queue_command_ex("contis", 50);
            } else {
                queue_command_ex("stopcontis", 100);
            }
        }

        // B7: Język
        startX += btnW*1.25f + bGap;
        float langDropW = btnW * 1.15f;
        Rectangle langBox = { startX, btnY, langDropW, btnH };
        bool langHover = CheckCollisionPointRec(mouse, langBox);
        if (langHover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            MUTEX_LOCK(g_laser.lock); g_laser.lang_dropdown_open = !g_laser.lang_dropdown_open; MUTEX_UNLOCK(g_laser.lock);
        }
        DrawRectangleRounded(langBox, 0.22f, 4, langHover ? (Color){ 55, 75, 100, 255 } : (Color){ 36, 48, 65, 255 });
        DrawRectangleRoundedLines(langBox, 0.22f, 4, (Color){ 100, 130, 160, 120 });
        DrawText(lang_names[lng], langBox.x + 8, langBox.y + (langBox.height - btnH*0.35f)/2, btnH*0.35f, WHITE);

        // B8: Pomoc [ ? ]
        Rectangle helpBtnRect = { startX + langDropW + bGap, btnY, btnH, btnH };
        if (DrawCustomButton(helpBtnRect, "?", (Color){ 70, 60, 30, 255 }, (Color){ 100, 85, 45, 255 }, btnH * 0.5f)) {
            MUTEX_LOCK(g_laser.lock); g_laser.show_help = !g_laser.show_help; MUTEX_UNLOCK(g_laser.lock);
        }

        // =========================================================================
        // 2. WYŚWIETLACZ 7-SEG, BŁĘDY I TELEMETRIA
        // =========================================================================
        int dBoxX = sw * 0.02f;
        DrawRectangleRounded((Rectangle){ dBoxX, mainY, dispW, dispH }, 0.035f, 4, (Color){ 10, 14, 20, 255 });
        DrawRectangleRoundedLines((Rectangle){ dBoxX, mainY, dispW, dispH }, 0.035f, 4, (Color){ 45, 68, 95, 220 });

        int numH = dispH * 0.46f;
        int center7SegY = mainY + dispH * 0.36f;

        if (!is_connected) {
            Draw7SegString(dBoxX + dispW*0.48f, center7SegY, numH, "----", (Color){ 160, 45, 45, 255 });
            DrawText(txt_lost[lng], dBoxX + dispW*0.32f, mainY + dispH*0.62f, dispH*0.065f, (Color){ 200, 65, 65, 255 });
        } else if (err_code != 0) {
            char errDisp[16]; sprintf(errDisp, "E-%d", err_code);
            Draw7SegString(dBoxX + dispW*0.48f, center7SegY, numH, errDisp, (Color){ 245, 140, 35, 255 });
            
            Rectangle errBanner = { dBoxX + dispW*0.05f, mainY + dispH*0.60f, dispW*0.90f, dispH*0.11f };
            DrawRectangleRounded(errBanner, 0.25f, 4, (Color){ 65, 25, 20, 255 });
            DrawRectangleRoundedLines(errBanner, 0.25f, 4, (Color){ 245, 120, 40, 200 });
            const char* errExplanation = get_error_desc(err_code, lng);
            int expW = MeasureText(errExplanation, dispH*0.055f);
            DrawText(errExplanation, errBanner.x + (errBanner.width - expW)/2, errBanner.y + dispH*0.025f, dispH*0.055f, (Color){ 255, 190, 80, 255 });
        } else if (is_valid_dist) {
            char distStr[16]; sprintf(distStr, "%d", current_dist);
            Draw7SegString(dBoxX + dispW*0.44f, center7SegY, numH, distStr, (Color){ 0, 245, 210, 255 });
            DrawText("mm", dBoxX + dispW*0.82f, center7SegY, numH * 0.28f, (Color){ 130, 165, 185, 255 });
            DrawText(TextFormat("%.3f m", current_dist / 1000.0f), dBoxX + dispW*0.74f, mainY + dispH*0.12f, dispH*0.08f, (Color){ 95, 235, 150, 255 });
        } else {
            Draw7SegString(dBoxX + dispW*0.48f, center7SegY, numH, "----", (Color){ 90, 110, 130, 255 });
            DrawText(txt_standby[lng], dBoxX + dispW*0.35f, mainY + dispH*0.62f, dispH*0.065f, (Color){ 110, 140, 170, 255 });
        }

        // Telemetria (S, N, I)
        int telemY = mainY + dispH * 0.75f;
        int telemH = dispH * 0.20f;
        DrawLine(dBoxX + dispW*0.04f, telemY, dBoxX + dispW*0.96f, telemY, (Color){ 35, 50, 70, 255 });

        Color sigColor = (sig_mv > 800) ? (Color){ 46, 204, 113, 255 } : (sig_mv > 300) ? (Color){ 241, 196, 15, 255 } : (Color){ 231, 76, 60, 255 };
        DrawText(TextFormat("SIGNAL (S): %d mV", sig_mv), dBoxX + dispW*0.05f, telemY + telemH*0.15f, telemH*0.28f, (Color){ 190, 215, 235, 255 });
        
        Rectangle barBg = { dBoxX + dispW*0.05f, telemY + telemH*0.52f, dispW*0.40f, telemH*0.28f };
        DrawRectangleRec(barBg, (Color){ 25, 35, 48, 255 });
        float sigRatio = sig_mv / 2500.0f;
        if (sigRatio > 1.0f) sigRatio = 1.0f;
        DrawRectangle(barBg.x, barBg.y, (int)(barBg.width * sigRatio), barBg.height, sigColor);
        DrawRectangleLinesEx(barBg, 1, (Color){ 60, 85, 115, 255 });

        DrawText(TextFormat("NOISE (N): %d mV", noise_mv), dBoxX + dispW*0.52f, telemY + telemH*0.15f, telemH*0.28f, (Color){ 190, 215, 235, 255 });
        DrawText(TextFormat("INTERNAL (I): %d mV", g_laser.internal_mv), dBoxX + dispW*0.52f, telemY + telemH*0.52f, telemH*0.25f, (Color){ 120, 155, 185, 255 });

        // =========================================================================
        // 3. WYKRES TRENDU
        // =========================================================================
        DrawRectangleRounded((Rectangle){ graphX, mainY, graphW, dispH }, 0.035f, 4, (Color){ 10, 14, 20, 255 });
        DrawRectangleRoundedLines((Rectangle){ graphX, mainY, graphW, dispH }, 0.035f, 4, (Color){ 45, 68, 95, 220 });
        DrawText("REAL-TIME TREND (DISTANCE OVER TIME)", graphX + 16, mainY + 12, dispH*0.055f, (Color){ 120, 155, 185, 255 });

        MUTEX_LOCK(g_laser.lock);
        float graph_max = 3000.0f;
        for(int i=0; i<HISTORY_LEN; i++) if (g_laser.history[i] > graph_max && g_laser.history[i] < 45000) graph_max = g_laser.history[i] + 500;
        
        for (int i = 0; i < HISTORY_LEN - 1; i++) {
            int idx = (g_laser.history_head + i) % HISTORY_LEN;
            int nidx = (g_laser.history_head + i + 1) % HISTORY_LEN;
            if (g_laser.history[idx] > 0 && g_laser.history[nidx] > 0) {
                float x1 = graphX + 10 + ((float)i / HISTORY_LEN) * (graphW - 20);
                float x2 = graphX + 10 + ((float)(i+1) / HISTORY_LEN) * (graphW - 20);
                float y1 = (mainY + dispH - 15) - ((float)g_laser.history[idx] / graph_max) * (dispH - 45);
                float y2 = (mainY + dispH - 15) - ((float)g_laser.history[nidx] / graph_max) * (dispH - 45);
                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 2.4f, (Color){ 0, 245, 210, 255 });
            }
        }
        MUTEX_UNLOCK(g_laser.lock);

        // =========================================================================
        // 4. ANIMOWANY TRACKER ODLEGLOŚCI
        // =========================================================================
        int sensorX = sw * 0.08f; 
        int trackMaxW = sw - sensorX - (sw * 0.08f);
        int trackY = vizY + vizH * 0.35f;

        float max_scale_mm = 10000.0f; 
        if (visual_dist_mm > 10000.0f) max_scale_mm = 20000.0f;
        if (visual_dist_mm > 20000.0f) max_scale_mm = 30000.0f;
        if (visual_dist_mm > 30000.0f) max_scale_mm = 40000.0f;

        float ratio = visual_dist_mm / max_scale_mm;
        if (ratio > 1.0f) ratio = 1.0f; 
        if (ratio < 0.02f) ratio = 0.02f;
        int targetX = sensorX + (int)(ratio * trackMaxW);

        DrawLineEx((Vector2){ sensorX, trackY + vizH*0.35f }, (Vector2){ sensorX + trackMaxW, trackY + vizH*0.35f }, 4, (Color){ 45, 65, 90, 255 });
        
        int step = max_scale_mm / 10;
        for (int m = 0; m <= max_scale_mm; m += step) {
            float tickR = (float)m / max_scale_mm;
            int tickX = sensorX + (int)(tickR * trackMaxW);
            DrawLineEx((Vector2){ tickX, trackY + vizH*0.25f }, (Vector2){ tickX, trackY + vizH*0.45f }, 2, (Color){ 70, 95, 125, 255 });
            DrawText(TextFormat("%.0fm", (float)m/1000), tickX - 10, trackY + vizH*0.52f, vizH*0.13f, (Color){ 100, 130, 160, 255 });
        }

        int modSize = vizH * 0.72f;
        DrawRectangleRounded((Rectangle){ sensorX - modSize, trackY - modSize*0.4f, modSize, modSize }, 0.1f, 4, (Color){ 25, 35, 50, 255 });
        DrawRectangleLines((sensorX - modSize), trackY - modSize*0.4f, modSize, modSize, (Color){ 65, 90, 125, 255 });
        
        if (is_connected && (is_measuring || laser_on)) {
            float glowAlpha = 0.25f + 0.18f * sinf(pulse);
            DrawLineEx((Vector2){ sensorX, trackY }, (Vector2){ targetX, trackY }, 12, Fade((Color){ 255, 30, 30, 255 }, glowAlpha));
            DrawLineEx((Vector2){ sensorX, trackY }, (Vector2){ targetX, trackY }, 4, (Color){ 255, 170, 170, 255 });
            DrawCircle(targetX, trackY, 9 + 3.0f * sinf(pulse), (Color){ 255, 40, 40, 255 });
        }
        
        DrawRectangleRounded((Rectangle){ targetX, trackY - modSize*0.5f, modSize*0.26f, modSize }, 0.1f, 4, (Color){ 140, 165, 185, 255 });
        DrawRectangleLines(targetX, trackY - modSize*0.5f, modSize*0.26f, modSize, (Color){ 200, 225, 245, 255 });

        // =========================================================================
        // 5. TERMINAL DZIENNIKA ZDARZEŃ
        // =========================================================================
        DrawRectangle(0, logY, sw, logH, (Color){ 10, 14, 20, 255 });
        DrawLine(0, logY, sw, logY, (Color){ 45, 68, 92, 255 });
        DrawText(TextFormat("> %s", txt_logs[lng]), sw*0.02f, logY + logH*0.12f, logH*0.15f, (Color){ 130, 165, 190, 255 });

        MUTEX_LOCK(g_laser.lock);
        for (int i = 0; i < g_laser.log_count; i++) {
            Color txtCol = (Color){ 175, 198, 220, 255 };
            if (strstr(g_laser.logs[i], "ERROR") || strstr(g_laser.logs[i], "BLAD") || strstr(g_laser.logs[i], "FOUT")) txtCol = (Color){ 245, 90, 80, 255 };
            else if (strstr(g_laser.logs[i], "TX")) txtCol = (Color){ 245, 185, 65, 255 };
            else if (strstr(g_laser.logs[i], "STATUS") || strstr(g_laser.logs[i], "PORT") || strstr(g_laser.logs[i], "BASE")) txtCol = (Color){ 80, 220, 140, 255 };
            DrawText(g_laser.logs[i], sw*0.02f, logY + logH*0.35f + (i * logH*0.10f), logH*0.11f, txtCol);
        }
        MUTEX_UNLOCK(g_laser.lock);

        // =========================================================================
        // 6. MODAL JĘZYKA I POMOCY
        // =========================================================================
        if (dropdown_open) {
            for (int i = 0; i < 3; i++) {
                Rectangle optBox = { startX, btnY + btnH + 4 + (i * btnH), langDropW, btnH };
                bool optHover = CheckCollisionPointRec(mouse, optBox);
                
                if (optHover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    MUTEX_LOCK(g_laser.lock);
                    g_laser.lang = (Language)i;
                    g_laser.lang_dropdown_open = false;
                    MUTEX_UNLOCK(g_laser.lock);
                }

                DrawRectangleRec(optBox, optHover ? (Color){ 55, 80, 115, 255 } : (Color){ 25, 35, 48, 255 });
                DrawRectangleLinesEx(optBox, 1, (Color){ 80, 110, 140, 180 });
                DrawText(lang_names[i], optBox.x + 10, optBox.y + (optBox.height - btnH*0.35f)/2, btnH*0.35f, (i == lng) ? (Color){ 0, 245, 210, 255 } : WHITE);
            }
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !CheckCollisionPointRec(mouse, (Rectangle){ startX, btnY, langDropW, btnH * 4 })) {
                MUTEX_LOCK(g_laser.lock); g_laser.lang_dropdown_open = false; MUTEX_UNLOCK(g_laser.lock);
            }
        }

        if (show_help) {
            DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 195 });
            int pW = sw * 0.65f; int pH = sh * 0.65f;
            int pX = (sw - pW) / 2; int pY = (sh - pH) / 2;
            
            DrawRectangleRounded((Rectangle){ pX, pY, pW, pH }, 0.03f, 4, (Color){ 18, 24, 32, 255 });
            DrawRectangleRoundedLines((Rectangle){ pX, pY, pW, pH }, 0.03f, 4, (Color){ 0, 245, 210, 255 });
            
            DrawText(txt_help_title[lng], pX + pW*0.05f, pY + pH*0.06f, pH*0.052f, (Color){ 0, 245, 210, 255 });
            DrawLine(pX + pW*0.05f, pY + pH*0.13f, pX + pW*0.95f, pY + pH*0.13f, (Color){ 45, 65, 90, 255 });
            DrawText(txt_help_body[lng], pX + pW*0.05f, pY + pH*0.18f, pH*0.032f, (Color){ 200, 220, 235, 255 });
            
            Rectangle closeHelpBtn = { pX + pW*0.35f, pY + pH*0.84f, pW*0.30f, pH*0.11f };
            if (DrawCustomButton(closeHelpBtn, txt_close_btn[lng], (Color){ 170, 45, 45, 255 }, (Color){ 205, 65, 65, 255 }, pH*0.042f)) {
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
    UnloadSound(blipSound); 
    CloseAudioDevice(); 
    CloseWindow();
    return 0;
}