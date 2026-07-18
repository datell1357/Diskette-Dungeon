// sokol 구현부 — 이 TU만 (mac에선 -x objective-c 로) 컴파일
#if defined(__APPLE__)
#define SOKOL_METAL
#elif defined(_WIN32)
#define SOKOL_GLCORE
#else
#define SOKOL_GLCORE
#endif
#define SOKOL_APP_IMPL
#define SOKOL_GFX_IMPL
#define SOKOL_GLUE_IMPL
#define SOKOL_LOG_IMPL
#define SOKOL_TIME_IMPL
#define SOKOL_AUDIO_IMPL
#define SOKOL_GL_IMPL
#include "vendor/sokol_app.h"
#include "vendor/sokol_gfx.h"
#include "vendor/sokol_glue.h"
#include "vendor/sokol_log.h"
#include "vendor/sokol_time.h"
#include "vendor/sokol_audio.h"
#include "vendor/sokol_gl.h"
