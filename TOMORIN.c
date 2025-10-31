// 渟雲. Released to Public Domain.
//
// -----------------------------------------------------------------------------
// File: TOMORIN.c
// Author: 渟雲(quq[at]outlook.it)
// Date: 2025-10-30
//
// -----------------------------------------------------------------------------
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#define NO_MIN_MAX
#include <string.h>
#include <time.h>
#include <windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "./stb_image.h"

#define M4P_IMPLEMENTATION
#include "./hitoshizuku.h"
#include "./gugugag.h"
#include "./m4p.h"

#pragma comment(lib, "winmm.lib")

static const char kCharSet[] = "DY14UF3RHWCXLQB6IKJT9N5AGS2PM8VZ7E";
enum { kBuffersNumber = 4, kBufferSize = 4096 };

static HWND g_edit_key_handle;
static HWND g_edit_licence_handle;
static HWND g_edit_expire_handle;
static HWND g_edit_maintain_handle;
static int g_current_version = 2;
static HBRUSH g_background_brush_handle;

static int g_img_width, g_img_height, g_img_channels;
static unsigned char* g_img_data = NULL;
static HBITMAP g_bitmap_handle = NULL;
static BOOL g_is_mirrored = FALSE;

volatile int g_keep_running = 1;
CRITICAL_SECTION g_audio_critical_section;

static float g_color_phase = 0.0f;
static COLORREF g_current_color = RGB(255, 0, 0);

typedef struct {
  HWAVEOUT wave_out_handle;
  WAVEFORMATEX wave_format;
  int is_playing;
  int sample_rate;

  float* audio_buffers[kBuffersNumber];
  int16_t* pcm_buffers[kBuffersNumber];
  WAVEHDR wave_headers[kBuffersNumber];
  int current_buffer;
  int buffers_queued;
} AudioContext;

AudioContext g_audio_context;

static void CALLBACK WaveOutProc(HWAVEOUT wave_out_handle, UINT message,
                                 DWORD_PTR instance, DWORD_PTR param1,
                                 DWORD_PTR param2) {
  if (message == WOM_DONE && g_keep_running) {
    AudioContext* audio = (AudioContext*)instance;
    WAVEHDR* completed_header = (WAVEHDR*)param1;

    EnterCriticalSection(&g_audio_critical_section);

    audio->buffers_queued--;

    if (audio->is_playing && !m4p_AtEnd() &&
        audio->buffers_queued < kBuffersNumber - 1) {
      m4p_GenerateFloatSamples(audio->audio_buffers[completed_header->dwUser],
                               kBufferSize);

      for (int i = 0; i < kBufferSize * 2; i++) {
        float sample = audio->audio_buffers[completed_header->dwUser][i];
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        audio->pcm_buffers[completed_header->dwUser][i] =
            (int16_t)(sample * 32767.0f);
      }

      completed_header->dwBufferLength = kBufferSize * 2 * sizeof(int16_t);
      MMRESULT result = waveOutWrite(audio->wave_out_handle, completed_header,
                                     sizeof(WAVEHDR));
      if (result == MMSYSERR_NOERROR) {
        audio->buffers_queued++;
      }
    }

    LeaveCriticalSection(&g_audio_critical_section);
  }
}

int AudioInit(AudioContext* audio, int sample_rate) {
  memset(audio, 0, sizeof(AudioContext));

  audio->sample_rate = sample_rate;
  audio->current_buffer = 0;
  audio->buffers_queued = 0;

  InitializeCriticalSection(&g_audio_critical_section);

  for (int i = 0; i < kBuffersNumber; i++) {
    audio->audio_buffers[i] = (float*)malloc(kBufferSize * 2 * sizeof(float));
    audio->pcm_buffers[i] = (int16_t*)malloc(kBufferSize * 2 * sizeof(int16_t));

    if (!audio->audio_buffers[i] || !audio->pcm_buffers[i]) {
      return 0;
    }

    memset(audio->audio_buffers[i], 0, kBufferSize * 2 * sizeof(float));
    memset(audio->pcm_buffers[i], 0, kBufferSize * 2 * sizeof(int16_t));
  }

  memset(&audio->wave_format, 0, sizeof(WAVEFORMATEX));
  audio->wave_format.wFormatTag = WAVE_FORMAT_PCM;
  audio->wave_format.nChannels = 2;
  audio->wave_format.nSamplesPerSec = sample_rate;
  audio->wave_format.wBitsPerSample = 16;
  audio->wave_format.nBlockAlign =
      audio->wave_format.nChannels * audio->wave_format.wBitsPerSample / 8;
  audio->wave_format.nAvgBytesPerSec =
      audio->wave_format.nSamplesPerSec * audio->wave_format.nBlockAlign;

  MMRESULT result =
      waveOutOpen(&audio->wave_out_handle, WAVE_MAPPER, &audio->wave_format,
                  (DWORD_PTR)WaveOutProc, (DWORD_PTR)audio, CALLBACK_FUNCTION);

  if (result != MMSYSERR_NOERROR) {
    return 0;
  }

  for (int i = 0; i < kBuffersNumber; i++) {
    memset(&audio->wave_headers[i], 0, sizeof(WAVEHDR));
    audio->wave_headers[i].lpData = (LPSTR)audio->pcm_buffers[i];
    audio->wave_headers[i].dwBufferLength = kBufferSize * 2 * sizeof(int16_t);
    audio->wave_headers[i].dwUser = i;

    result = waveOutPrepareHeader(audio->wave_out_handle,
                                  &audio->wave_headers[i], sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
      return 0;
    }
  }

  return 1;
}

void AudioStart(AudioContext* audio) {
  if (!audio->is_playing) {
    audio->is_playing = 1;

    EnterCriticalSection(&g_audio_critical_section);

    for (int i = 0; i < kBuffersNumber; i++) {
      m4p_GenerateFloatSamples(audio->audio_buffers[i], kBufferSize);

      for (int j = 0; j < kBufferSize * 2; j++) {
        float sample = audio->audio_buffers[i][j];
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        audio->pcm_buffers[i][j] = (int16_t)(sample * 32767.0f);
      }

      MMRESULT result = waveOutWrite(audio->wave_out_handle,
                                     &audio->wave_headers[i], sizeof(WAVEHDR));
      if (result == MMSYSERR_NOERROR) {
        audio->buffers_queued++;
      }
    }

    LeaveCriticalSection(&g_audio_critical_section);
  }
}

void AudioCleanup(AudioContext* audio) {
  g_keep_running = 0;
  Sleep(100);

  if (audio->wave_out_handle) {
    waveOutReset(audio->wave_out_handle);

    for (int i = 0; i < kBuffersNumber; i++) {
      if (audio->wave_headers[i].dwFlags & WHDR_PREPARED) {
        waveOutUnprepareHeader(audio->wave_out_handle, &audio->wave_headers[i],
                               sizeof(WAVEHDR));
      }
    }

    waveOutClose(audio->wave_out_handle);
    audio->wave_out_handle = NULL;
  }

  for (int i = 0; i < kBuffersNumber; i++) {
    if (audio->audio_buffers[i]) {
      free(audio->audio_buffers[i]);
      audio->audio_buffers[i] = NULL;
    }
    if (audio->pcm_buffers[i]) {
      free(audio->pcm_buffers[i]);
      audio->pcm_buffers[i] = NULL;
    }
  }

  DeleteCriticalSection(&g_audio_critical_section);
  audio->is_playing = 0;
}

void StartBackgroundMusic() {
  if (!m4p_LoadFromData(kHitoShizuku, sizeof(kHitoShizuku), 44100, 16384)) {
    return;
  }

  if (!AudioInit(&g_audio_context, 44100)) {
    m4p_FreeSong();
    return;
  }

  m4p_PlaySong(1);
  AudioStart(&g_audio_context);
}

void StopBackgroundMusic() {
  AudioCleanup(&g_audio_context);
  m4p_Stop();
  m4p_Close();
  m4p_FreeSong();
}

void LoadImageFromMemory() {
  if (g_img_data) {
    stbi_image_free(g_img_data);
    g_img_data = NULL;
  }

  g_img_data = stbi_load_from_memory(kGugugaga, 2895, &g_img_width,
                                     &g_img_height, &g_img_channels, 3);
  if (g_img_data) {
    unsigned char* bgra_data =
        (unsigned char*)malloc(g_img_width * g_img_height * 4);
    for (int i = 0; i < g_img_width * g_img_height; ++i) {
      bgra_data[i * 4 + 0] = g_img_data[i * 3 + 2];
      bgra_data[i * 4 + 1] = g_img_data[i * 3 + 1];
      bgra_data[i * 4 + 2] = g_img_data[i * 3 + 0];
      bgra_data[i * 4 + 3] = 255;
    }

    stbi_image_free(g_img_data);
    g_img_data = bgra_data;
    g_img_channels = 4;

    if (g_bitmap_handle) {
      DeleteObject(g_bitmap_handle);
    }

    HDC screen_dc = GetDC(NULL);
    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = g_img_width;
    bitmap_info.bmiHeader.biHeight = -g_img_height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    g_bitmap_handle =
        CreateDIBitmap(screen_dc, &bitmap_info.bmiHeader, CBM_INIT, g_img_data,
                       &bitmap_info, DIB_RGB_COLORS);
    ReleaseDC(NULL, screen_dc);
  }
}

unsigned int StringToValue(const char* str) {
  unsigned int result = 0;
  size_t len = strlen(str);

  for (size_t i = 0; i < len; ++i) {
    int char_index = -1;
    for (int j = 0; j < 34; ++j) {
      if (str[i] == kCharSet[j]) {
        char_index = j;
        break;
      }
    }
    if (char_index == -1) continue;

    double weight = 1.0;
    int exponent = len - 1 - i;
    double base = 34.0;
    unsigned int exp = (exponent < 0) ? -exponent : exponent;

    while (exp > 0) {
      if (exp & 1) {
        weight *= base;
      }
      base *= base;
      exp >>= 1;
    }

    if (exponent < 0) {
      weight = 1.0 / weight;
    }

    result += char_index * (unsigned int)(weight);
  }

  return result;
}

void ValueToString(unsigned int value, int length, char* result) {
  for (int i = length - 1; i >= 0; --i) {
    result[i] = kCharSet[value % 34];
    value /= 34;
  }
  result[length] = '\0';
}

unsigned short CalculateChecksum(const char* data, int len) {
  unsigned short crc = 0;

  for (int i = 0; i < len; ++i) {
    crc ^= (data[i] << 8);

    for (int j = 0; j < 8; ++j) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x8201;
      } else {
        crc <<= 1;
      }
    }

    crc &= 0xFFFF;
  }

  return crc;
}

void EncodeChecksum(unsigned int checksum, char* result) {
  if (checksum <= 0x9987) {
    result[0] = kCharSet[checksum / 0x484];
    result[1] = kCharSet[(checksum % 0x484) / 0x22];
    result[2] = kCharSet[checksum % 0x22];
    result[3] = '\0';
  }
}

int EncodeTime() {
  time_t now = time(NULL);
  struct tm* time_info = localtime(&now);
  return time_info->tm_mday +
         32 * (16 * time_info->tm_year + time_info->tm_mon - 1647);
}

unsigned int GetUserValue(HWND edit_handle, unsigned int default_value, unsigned int max_value) {
  char buffer[32];
  GetWindowText(edit_handle, buffer, sizeof(buffer));

  if (strlen(buffer) == 0) {
    return default_value;
  }

  unsigned int value = (unsigned int)strtoul(buffer, NULL, 10);

  if (value > max_value) {
    value = max_value;
    char value_str[32];
    _itoa(value, value_str, 10);
    SetWindowText(edit_handle, value_str);
  }

  return value;
}

void GenerateKey(int version, int time_encoded, char* output) {
  char random_chars[3] = {0};
  random_chars[0] = kCharSet[rand() % 34];
  random_chars[1] = kCharSet[rand() % 34];

  unsigned int base_value = StringToValue(random_chars);
  unsigned int licence_count = GetUserValue(g_edit_licence_handle, 1, 797);
  unsigned int expire_days = GetUserValue(g_edit_expire_handle, 0, 3652);
  unsigned int maintain_days = GetUserValue(g_edit_maintain_handle, 3652, 3652);

  unsigned int licence_count_magic = licence_count ^ 0x4755;
  unsigned int expire_days_magic = expire_days ^ 0x3FD;
  unsigned int maintain_days_magic = maintain_days ^ 0x935;

  char parts[8][5] = {0};
  ValueToString(version ^ (base_value & 0xFF) ^ 0xBF, 2, parts[0]);
  ValueToString(base_value ^ 0x88, 2, parts[1]);
  ValueToString((base_value & 0xFF) ^ 0x77, 2, parts[2]);
  ValueToString((base_value & 0xFF) ^ 0xDD, 2, parts[3]);
  ValueToString(base_value ^ licence_count_magic, 4, parts[4]);
  ValueToString(time_encoded ^ base_value ^ 0x7CC1, 4, parts[5]);
  ValueToString((base_value & 0xFF) ^ expire_days_magic, 3, parts[6]);
  ValueToString((base_value & 0xFF) ^ maintain_days_magic, 3, parts[7]);

  char data_part[25] = {0};
  for (int i = 0; i < 8; i++) {
    strcat(data_part, parts[i]);
  }
  strcat(data_part, random_chars);

  unsigned short checksum = CalculateChecksum(data_part, 24);
  char check_chars[4] = {0};
  EncodeChecksum(checksum % 0x9987, check_chars);

  char full_key[26] = {0};
  strncpy(full_key, data_part, 24);
  full_key[24] = check_chars[1];
  full_key[25] = '\0';

  char formatted[30] = {0};
  int pos = 0;
  for (int i = 0; i < 25; ++i) {
    formatted[pos++] = full_key[i];
    if ((i + 1) % 5 == 0 && i < 24) {
      formatted[pos++] = '-';
    }
  }
  formatted[pos] = '\0';

  strcpy(output, formatted);
}

void GenerateKeyHandler() {
  int time_encoded = EncodeTime();
  char key[30];
  GenerateKey(g_current_version, time_encoded, key);
  SetWindowText(g_edit_key_handle, key);
}

void UpdateColorState() {
  g_color_phase += 0.3351f;
  if (g_color_phase > 2 * 3.14159f) {
    g_color_phase -= 2 * 3.14159f;
  }

  int r = (int)(127.5f + 127.5f * sin(g_color_phase));
  int g = (int)(127.5f + 127.5f * sin(g_color_phase + 2 * 3.14159f / 3));
  int b = (int)(127.5f + 127.5f * sin(g_color_phase + 4 * 3.14159f / 3));

  r = max(0, min(255, r));
  g = max(0, min(255, g));
  b = max(0, min(255, b));

  g_current_color = RGB(r, g, b);
}

COLORREF GetCurrentColor() { return g_current_color; }

LRESULT CALLBACK WndProc(HWND window_handle, UINT message, WPARAM w_param,
                         LPARAM l_param) {
  switch (message) {
    case WM_CREATE: {
      LoadImageFromMemory();
      StartBackgroundMusic();

      HFONT font_handle =
          CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     DEFAULT_QUALITY, DEFAULT_PITCH, "MS Shell Dlg");

      CreateWindow("STATIC", "Tomorin Stole Precious Thing!!",
                   WS_VISIBLE | WS_CHILD, 250, 15, 200, 20, window_handle, NULL,
                   NULL, NULL);

      CreateWindow("STATIC", "Licence Count (1-797):",
                   WS_VISIBLE | WS_CHILD, 250, 45, 150, 20, window_handle, NULL,
                   NULL, NULL);
      g_edit_licence_handle = CreateWindow(
          "EDIT", "797", WS_VISIBLE | WS_CHILD | WS_BORDER, 400, 45, 50, 20,
          window_handle, NULL, NULL, NULL);

      CreateWindow("STATIC", "Expire Days (0-3652):",
                   WS_VISIBLE | WS_CHILD, 250, 70, 150, 20, window_handle, NULL,
                   NULL, NULL);
      g_edit_expire_handle = CreateWindow(
          "EDIT", "0", WS_VISIBLE | WS_CHILD | WS_BORDER, 400, 70, 50, 20,
          window_handle, NULL, NULL, NULL);
      CreateWindow("STATIC", "Maintain (1-3652):",
                   WS_VISIBLE | WS_CHILD, 250, 95, 150, 20, window_handle, NULL,
                   NULL, NULL);
      g_edit_maintain_handle = CreateWindow(
          "EDIT", "3652", WS_VISIBLE | WS_CHILD | WS_BORDER, 400, 95, 50, 20,
          window_handle, NULL, NULL, NULL);
      const HWND radio_button_1 =
          CreateWindow("BUTTON", "AIDA64 Business",
                       WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 250, 120, 150,
                       20, window_handle, (HMENU)1, NULL, NULL);
      const HWND radio_button_2 =
          CreateWindow("BUTTON", "AIDA64 Extreme",
                       WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 250, 145, 150,
                       20, window_handle, (HMENU)2, NULL, NULL);
      const HWND radio_button_3 =
          CreateWindow("BUTTON", "AIDA64 Engineer",
                       WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 250, 170, 150,
                       20, window_handle, (HMENU)3, NULL, NULL);
      const HWND radio_button_4 =
          CreateWindow("BUTTON", "AIDA64 Network Audit",
                       WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 250, 195,
                       150, 20, window_handle, (HMENU)4, NULL, NULL);

      g_edit_key_handle = CreateWindow(
          "EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY, 250, 220,
          200, 25, window_handle, NULL, NULL, NULL);

      const HWND generate_button =
          CreateWindow("BUTTON", "Generate", WS_VISIBLE | WS_CHILD, 400, 120,
                       60, 30, window_handle, (HMENU)5, NULL, NULL);
      const HWND about_button =
          CreateWindow("BUTTON", "About", WS_VISIBLE | WS_CHILD, 400, 152, 60,
                       30, window_handle, (HMENU)7, NULL, NULL);
      const HWND exit_button =
          CreateWindow("BUTTON", "Exit", WS_VISIBLE | WS_CHILD, 400, 184, 60,
                       30, window_handle, (HMENU)6, NULL, NULL);

      SendMessage(radio_button_1, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(radio_button_2, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(radio_button_3, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(radio_button_4, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(g_edit_licence_handle, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(g_edit_expire_handle, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(g_edit_maintain_handle, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(g_edit_key_handle, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(generate_button, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(about_button, WM_SETFONT, (WPARAM)font_handle, TRUE);
      SendMessage(exit_button, WM_SETFONT, (WPARAM)font_handle, TRUE);

      CheckRadioButton(window_handle, 1, 4, 2);

      GenerateKeyHandler();
      break;
    }
    case WM_NCHITTEST: {
      LRESULT hit = DefWindowProc(window_handle, message, w_param, l_param);
      if (hit == HTCLIENT) {
        return HTCAPTION;
      }
      return hit;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
      HDC device_context = (HDC)w_param;
      SetBkMode(device_context, TRANSPARENT);
      SetTextColor(device_context, RGB(0, 0, 0));
      return (LRESULT)g_background_brush_handle;
    }

    case WM_COMMAND: {
      int id = LOWORD(w_param);
      switch (id) {
        case 1:
        case 2:
        case 3:
        case 4:
          g_current_version = id;
          CheckRadioButton(window_handle, 1, 4, id);
          GenerateKeyHandler();
          break;
        case 5:
          GenerateKeyHandler();
          break;
        case 6:
          PostQuitMessage(0);
          break;
        case 7:
          MessageBoxW(window_handle,
                      L"Author: \u6e1f\u96f2\n"
                      L"Released to Public Domain\n"
                      L"Tomorin Stole Precious Thing!!\n"
                      L"Version v1.1.1\n"
                      L"BGM: \u58f1\u96eb\u7a7a"
                      L"  Copyright (C) 2024 by qiaokong\n",
                      L"About", MB_OK | MB_ICONINFORMATION);
          break;
      }
      break;
    }
    case WM_TIMER: {
      g_is_mirrored = !g_is_mirrored;
      UpdateColorState();

      DeleteObject(g_background_brush_handle);
      g_background_brush_handle = CreateSolidBrush(GetCurrentColor());

      InvalidateRect(window_handle, NULL, TRUE);
      break;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint_struct;
      HDC device_context = BeginPaint(window_handle, &paint_struct);

      RECT rect;
      GetClientRect(window_handle, &rect);
      FillRect(device_context, &rect, g_background_brush_handle);

      if (g_bitmap_handle) {
        HDC memory_dc = CreateCompatibleDC(device_context);
        SelectObject(memory_dc, g_bitmap_handle);
        if (g_is_mirrored) {
          StretchBlt(device_context, g_img_width, 0, -g_img_width, g_img_height,
                     memory_dc, 0, 0, g_img_width, g_img_height, SRCCOPY);
        } else {
          BitBlt(device_context, 0, 0, g_img_width, g_img_height, memory_dc, 0,
                 0, SRCCOPY);
        }
        DeleteDC(memory_dc);
      }

      HDC memory_dc = CreateCompatibleDC(device_context);
      HBITMAP bitmap_handle = CreateBitmap(230, 250, 1, 24, kGugugaga);
      SelectObject(memory_dc, bitmap_handle);
      BitBlt(device_context, 0, 0, 230, 250, memory_dc, 0, 0, SRCCOPY);
      DeleteObject(bitmap_handle);
      DeleteDC(memory_dc);

      EndPaint(window_handle, &paint_struct);
      break;
    }

    case WM_CLOSE:
      PostQuitMessage(0);
      break;

    case WM_DESTROY:
      StopBackgroundMusic();
      if (g_img_data) {
        stbi_image_free(g_img_data);
        g_img_data = NULL;
      }
      if (g_bitmap_handle) {
        DeleteObject(g_bitmap_handle);
        g_bitmap_handle = NULL;
      }
      KillTimer(window_handle, 1);
      DeleteObject(g_background_brush_handle);
      PostQuitMessage(0);
      break;

    default:
      return DefWindowProc(window_handle, message, w_param, l_param);
  }
  return 0;
}

int WINAPI WinMain(HINSTANCE instance_handle, HINSTANCE prev_instance_handle,
                   LPSTR command_line, int show_command) {
  srand((unsigned int)time(NULL));

  WNDCLASS window_class = {0};
  window_class.lpfnWndProc = WndProc;
  window_class.hInstance = instance_handle;
  window_class.lpszClassName = "TOMORIN";
  window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
  window_class.style = CS_HREDRAW | CS_VREDRAW;

  RegisterClass(&window_class);

  g_background_brush_handle = CreateSolidBrush(RGB(240, 240, 240));

  HWND window_handle = CreateWindow("TOMORIN", "Tomorin Stole Precious Thing!! v1.1.1",
                                    WS_POPUP | WS_VISIBLE, 300, 300, 480, 250,
                                    NULL, NULL, instance_handle, NULL);
  SetTimer(window_handle, 1, 294, NULL);
  ShowWindow(window_handle, show_command);
  UpdateWindow(window_handle);

  MSG message;
  while (GetMessage(&message, NULL, 0, 0)) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  return (int)message.wParam;
}
