/*
Copyright (c) 2022-2023 Clownacy

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
*/

/*
ClownResampler

This is a single-file library for resampling audio. It is written in C89 and
licensed under the terms of the Zero-Clause BSD licence.

In particular, this library implements a windowed-sinc resampler, using a
Lanczos window.

https://github.com/Clownacy/clownresampler
*/

/*
Contents:
- 1. Examples
- 2. Configuration
- 3. Header & Documentation
- 4. Implementation
*/

/* 1. Examples */

#if 0
/*
This demonstrates use of ClownResampler's low-level API.

The low-level API is ideal for when the entirety of the input data is available
at once, whereas the high-level API is ideal for when the input data is
streamed piece by piece.
*/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_API static
#include "libraries/miniaudio.h" /* v0.11.9 */

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_ONLY_MP3
#define DRMP3_API static
#include "libraries/dr_mp3.h" /* v0.6.33 */

#define CLOWNRESAMPLER_IMPLEMENTATION
#define CLOWNRESAMPLER_STATIC
#define CLOWNRESAMPLER_NO_HIGH_LEVEL_API /* We do not need the high-level API. */
#include "../clownresampler.h"

static ClownResampler_Precomputed precomputed;
static ClownResampler_LowLevel_State resampler;
static unsigned int total_channels;
static drmp3_int16 *resampler_input_buffer;
static size_t resampler_input_buffer_total_frames;
static size_t resampler_input_buffer_frames_remaining;

typedef struct ResamplerCallbackData
{
	ma_int16 *output_pointer;
	ma_uint32 output_buffer_frames_remaining;
} ResamplerCallbackData;

static cc_bool ResamplerOutputCallback(void *user_data, const cc_s32f *frame, cc_u8f total_samples)
{
	ResamplerCallbackData* const callback_data = (ResamplerCallbackData*)user_data;

	cc_u8f i;

	/* Output the frame. */
	for (i = 0; i < total_samples; ++i)
	{
		cc_s32f sample;

		sample = frame[i];

		/* Clamp the sample to 16-bit. */
		if (sample > 0x7FFF)
			sample = 0x7FFF;
		else if (sample < -0x7FFF)
			sample = -0x7FFF;

		/* Push the sample to the output buffer. */
		*callback_data->output_pointer++ = (ma_int16)sample;
	}

	/* Signal whether there is more room in the output buffer. */
	return --callback_data->output_buffer_frames_remaining != 0;
}

static void AudioCallback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
{
	ResamplerCallbackData callback_data;

	(void)device;
	(void)input;

	callback_data.output_pointer = (ma_int16*)output;
	callback_data.output_buffer_frames_remaining = frame_count;

	/* Resample the decoded audio data. */
	ClownResampler_LowLevel_Resample(&resampler, &precomputed, &resampler_input_buffer[(resampler_input_buffer_total_frames - resampler_input_buffer_frames_remaining) * total_channels], &resampler_input_buffer_frames_remaining, ResamplerOutputCallback, &callback_data);

	/* If there are no more samples left, then fill the remaining space in the buffer with 0. */
	memset(callback_data.output_pointer, 0, callback_data.output_buffer_frames_remaining * total_channels * sizeof(ma_int16));
}

int main(int argc, char **argv)
{
	int exit_code;
	drmp3 mp3_decoder;

	exit_code = EXIT_FAILURE;

	if (argc < 2)
	{
		fputs("Pass the path to an MP3 file as an argument.\n", stderr);
	}
	else
	{
		if (!drmp3_init_file(&mp3_decoder, argv[1], NULL))
		{
			fputs("Failed to initialise MP3 decoder.\n", stderr);
		}
		else
		{
			/******************************/
			/* Initialise audio playback. */
			/******************************/
			ma_device_config miniaudio_config;
			ma_device miniaudio_device;

			miniaudio_config = ma_device_config_init(ma_device_type_playback);
			miniaudio_config.playback.format   = ma_format_s16;
			miniaudio_config.playback.channels = mp3_decoder.channels;
			miniaudio_config.sampleRate        = 0; /* Use whatever sample rate the playback device wants. */
			miniaudio_config.dataCallback      = AudioCallback;
			miniaudio_config.pUserData         = NULL;

			if (ma_device_init(NULL, &miniaudio_config, &miniaudio_device) != MA_SUCCESS)
			{
				drmp3_uninit(&mp3_decoder);
				fputs("Failed to initialise playback device.\n", stderr);
			}
			else
			{
				/*****************************************/
				/* Finished initialising audio playback. */
				/*****************************************/

				const size_t size_of_frame = mp3_decoder.channels * sizeof(drmp3_int16);

				size_t total_mp3_pcm_frames;

				total_mp3_pcm_frames = drmp3_get_pcm_frame_count(&mp3_decoder);
				total_channels = mp3_decoder.channels;

				/* Inform the user of the input and output sample rates. */
				fprintf(stderr, "MP3 Sample Rate: %lu\n", (unsigned long)mp3_decoder.sampleRate);
				fprintf(stderr, "Playback Sample Rate: %lu\n", (unsigned long)miniaudio_device.sampleRate);
				fflush(stderr);

				/******************************/
				/* Initialise ClownResampler. */
				/******************************/

				/* Precompute the Lanczos kernel. */
				ClownResampler_Precompute(&precomputed);

				/* Create a resampler that converts from the sample rate of the MP3 to the sample rate of the playback device. */
				/* The low-pass filter is set to 44100Hz since that should allow all human-perceivable frequencies through. */
				ClownResampler_LowLevel_Init(&resampler, mp3_decoder.channels, mp3_decoder.sampleRate, miniaudio_device.sampleRate, 44100);

				/*****************************************/
				/* Finished initialising ClownResampler. */
				/*****************************************/

				/*****************************************/
				/* Set up ClownResampler's input buffer. */
				/*****************************************/

				/* Create a buffer to hold the decoded PCM data. */
				/* ClownResampler's low-level API requires that this buffer have padding at its beginning and end. */
				resampler_input_buffer = (drmp3_int16*)malloc((resampler.lowest_level.integer_stretched_kernel_radius * 2 + total_mp3_pcm_frames) * size_of_frame);

				if (resampler_input_buffer == NULL)
				{
					drmp3_uninit(&mp3_decoder);
					fputs("Failed to allocate memory for resampler input buffer.\n", stderr);
				}
				else
				{
					/* Set the padding samples at the start to 0. */
					memset(&resampler_input_buffer[0], 0, resampler.lowest_level.integer_stretched_kernel_radius * size_of_frame);

					/* Decode the MP3 to the input buffer. */
					drmp3_read_pcm_frames_s16(&mp3_decoder, total_mp3_pcm_frames, &resampler_input_buffer[resampler.lowest_level.integer_stretched_kernel_radius * total_channels]);
					drmp3_uninit(&mp3_decoder);

					/* Set the padding samples at the end to 0. */
					memset(&resampler_input_buffer[(resampler.lowest_level.integer_stretched_kernel_radius + total_mp3_pcm_frames) * total_channels], 0, resampler.lowest_level.integer_stretched_kernel_radius * size_of_frame);

					/* Initialise some variables that will be used by the audio callback. */
					resampler_input_buffer_total_frames = resampler_input_buffer_frames_remaining = total_mp3_pcm_frames;

					/*****************************************************/
					/* Finished setting up the resampler's input buffer. */
					/*****************************************************/

					/* Begin playback. */
					ma_device_start(&miniaudio_device);

					/* Wait for input from the user before terminating the program. */
					fgetc(stdin);

					ma_device_stop(&miniaudio_device);

					free(resampler_input_buffer);

					exit_code = EXIT_SUCCESS;
				}

				ma_device_uninit(&miniaudio_device);
			}
		}
	}

	return exit_code;
}
#endif

#if 0
/*
This demonstrates use of ClownResampler's high-level API.

The low-level API is ideal for when the entirety of the input data is available
at once, whereas the high-level API is ideal for when the input data is
streamed piece by piece.
*/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_API static
#include "libraries/miniaudio.h" /* v0.11.9 */

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_ONLY_MP3
#define DRMP3_API static
#include "libraries/dr_mp3.h" /* v0.6.33 */

#define CLOWNRESAMPLER_IMPLEMENTATION
#define CLOWNRESAMPLER_STATIC
#include "../clownresampler.h"

static ClownResampler_Precomputed precomputed;
static ClownResampler_HighLevel_State resampler;
static drmp3 mp3_decoder;
static unsigned int total_channels;

typedef struct ResamplerCallbackData
{
	ma_int16 *output_pointer;
	ma_uint32 output_buffer_frames_remaining;
} ResamplerCallbackData;

static size_t ResamplerInputCallback(void *user_data, cc_s16l *buffer, size_t total_frames)
{
	(void)user_data;

	/* Obtain samples from the MP3 file. */
	return drmp3_read_pcm_frames_s16(&mp3_decoder, total_frames, buffer);
}

static cc_bool ResamplerOutputCallback(void *user_data, const cc_s32f *frame, cc_u8f total_samples)
{
	ResamplerCallbackData* const callback_data = (ResamplerCallbackData*)user_data;

	cc_u8f i;

	/* Output the frame. */
	for (i = 0; i < total_samples; ++i)
	{
		cc_s32f sample;

		sample = frame[i];

		/* Clamp the sample to 16-bit. */
		if (sample > 0x7FFF)
			sample = 0x7FFF;
		else if (sample < -0x7FFF)
			sample = -0x7FFF;

		/* Push the sample to the output buffer. */
		*callback_data->output_pointer++ = (ma_int16)sample;
	}

	/* Signal whether there is more room in the output buffer. */
	return --callback_data->output_buffer_frames_remaining != 0;
}

static void AudioCallback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
{
	ResamplerCallbackData callback_data;

	(void)device;
	(void)input;

	callback_data.output_pointer = (ma_int16*)output;
	callback_data.output_buffer_frames_remaining = frame_count;

	/* Resample the decoded audio data. */
	ClownResampler_HighLevel_Resample(&resampler, &precomputed, ResamplerInputCallback, ResamplerOutputCallback, &callback_data);

	/* If there are no more samples left, then fill the remaining space in the buffer with 0. */
	memset(callback_data.output_pointer, 0, callback_data.output_buffer_frames_remaining * total_channels * sizeof(ma_int16));
}

int main(int argc, char **argv)
{
	int exit_code;

	exit_code = EXIT_FAILURE;

	if (argc < 2)
	{
		fputs("Pass the path to an MP3 file as an argument.\n", stderr);
	}
	else
	{
		if (!drmp3_init_file(&mp3_decoder, argv[1], NULL))
		{
			fputs("Failed to initialise MP3 decoder.\n", stderr);
		}
		else
		{
			/******************************/
			/* Initialise audio playback. */
			/******************************/
			ma_device_config miniaudio_config;
			ma_device miniaudio_device;

			miniaudio_config = ma_device_config_init(ma_device_type_playback);
			miniaudio_config.playback.format   = ma_format_s16;
			miniaudio_config.playback.channels = mp3_decoder.channels;
			miniaudio_config.sampleRate        = 0; /* Use whatever sample rate the playback device wants. */
			miniaudio_config.dataCallback      = AudioCallback;
			miniaudio_config.pUserData         = NULL;

			if (ma_device_init(NULL, &miniaudio_config, &miniaudio_device) != MA_SUCCESS)
			{
				fputs("Failed to initialise playback device.\n", stderr);
			}
			else
			{
				/*****************************************/
				/* Finished initialising audio playback. */
				/*****************************************/

				/* Inform the user of the input and output sample rates. */
				fprintf(stderr, "MP3 Sample Rate: %lu\n", (unsigned long)mp3_decoder.sampleRate);
				fprintf(stderr, "Playback Sample Rate: %lu\n", (unsigned long)miniaudio_device.sampleRate);
				fflush(stderr);

				/******************************/
				/* Initialise ClownResampler. */
				/******************************/

				/* Precompute the Lanczos kernel. */
				ClownResampler_Precompute(&precomputed);

				/* Create a resampler that converts from the sample rate of the MP3 to the sample rate of the playback device. */
				/* The low-pass filter is set to 44100Hz since that should allow all human-perceivable frequencies through. */
				ClownResampler_HighLevel_Init(&resampler, mp3_decoder.channels, mp3_decoder.sampleRate, miniaudio_device.sampleRate, 44100);

				/*****************************************/
				/* Finished initialising ClownResampler. */
				/*****************************************/

				total_channels = mp3_decoder.channels;

				/* Begin playback. */
				ma_device_start(&miniaudio_device);

				/* Wait for input from the user before terminating the program. */
				fgetc(stdin);

				ma_device_uninit(&miniaudio_device);

				exit_code = EXIT_SUCCESS;
			}

			drmp3_uninit(&mp3_decoder);
		}
	}

	return exit_code;
}
#endif

#ifndef CLOWNRESAMPLER_GUARD_MISC
#define CLOWNRESAMPLER_GUARD_MISC


/* 2. Configuration */

/* Define 'CLOWNRESAMPLER_STATIC' to limit the visibility of public functions. */
/* Alternatively, define 'CLOWNRESAMPLER_API' to control the qualifiers applied to the public functions. */
#ifndef CLOWNRESAMPLER_API
 #ifdef CLOWNRESAMPLER_STATIC
  #define CLOWNRESAMPLER_API static
 #else
  #define CLOWNRESAMPLER_API
 #endif
#endif

/* Controls the number of 'lobes' of the windowed sinc function.
   A higher number results in better audio, but is more expensive. */
#ifndef CLOWNRESAMPLER_KERNEL_RADIUS
#define CLOWNRESAMPLER_KERNEL_RADIUS 3
#endif

/* How many samples to render per lobe for the pre-computed Lanczos kernel.
   Higher numbers produce a higher-quality Lanczos kernel, but cause it to take
   up more memory and cache. */
#ifndef CLOWNRESAMPLER_KERNEL_RESOLUTION
#define CLOWNRESAMPLER_KERNEL_RESOLUTION 0x400 /* 1024 samples per lobe should be more than good enough */
#endif

/* The maximum number of channels supported by the resampler.
   This will likely be removed in the future. */
#ifndef CLOWNRESAMPLER_MAXIMUM_CHANNELS
#define CLOWNRESAMPLER_MAXIMUM_CHANNELS 16 /* As stb_vorbis says, this should be enough for pretty much everyone. */
#endif

/* Disables the low-level API. */
/*#define CLOWNRESAMPLER_NO_LOW_LEVEL_API*/

/* Disables the high-level API. */
/*#define CLOWNRESAMPLER_NO_HIGH_LEVEL_API*/

/* Disables the ClownResampler_HighLevel_Adjust function. */
/*#define CLOWNRESAMPLER_NO_HIGH_LEVEL_ADJUST*/

/* Disables the ClownResampler_HighLevel_ResampleEnd function. */
/*#define CLOWNRESAMPLER_NO_HIGH_LEVEL_RESAMPLE_END*/


/* 3. Header & Documentation */

#include <stddef.h>

/* Integer types. */
#ifndef CC_INTEGERS_DEFINED
#define CC_INTEGERS_DEFINED

#if defined(CC_USE_C99_INTEGERS)
 /* Use C99's/C++11's better integer types if available. */
#include <stdint.h>

typedef int_least8_t cc_s8l;
typedef int_least16_t cc_s16l;
typedef int_least32_t cc_s32l;

typedef uint_least8_t cc_u8l;
typedef uint_least16_t cc_u16l;
typedef uint_least32_t cc_u32l;

typedef int_fast8_t cc_s8f;
typedef int_fast16_t cc_s16f;
typedef int_fast32_t cc_s32f;

typedef uint_fast8_t cc_u8f;
typedef uint_fast16_t cc_u16f;
typedef uint_fast32_t cc_u32f;

#define CC_PRIdLEAST8 PRIdLEAST8
#define CC_PRIiLEAST8 PRIiLEAST8
#define CC_PRIuLEAST8 PRIuLEAST8
#define CC_PRIoLEAST8 PRIoLEAST8
#define CC_PRIxLEAST8 PRIxLEAST8
#define CC_PRIXLEAST8 PRIXLEAST8

#define CC_PRIdLEAST16 PRIdLEAST16
#define CC_PRIiLEAST16 PRIiLEAST16
#define CC_PRIuLEAST16 PRIuLEAST16
#define CC_PRIoLEAST16 PRIoLEAST16
#define CC_PRIxLEAST16 PRIxLEAST16
#define CC_PRIXLEAST16 PRIXLEAST16

#define CC_PRIdLEAST32 PRIdLEAST32
#define CC_PRIiLEAST32 PRIiLEAST32
#define CC_PRIuLEAST32 PRIuLEAST32
#define CC_PRIoLEAST32 PRIoLEAST32
#define CC_PRIxLEAST32 PRIxLEAST32
#define CC_PRIXLEAST32 PRIXLEAST32

#define CC_PRIdFAST8 PRIdFAST8
#define CC_PRIiFAST8 PRIiFAST8
#define CC_PRIuFAST8 PRIuFAST8
#define CC_PRIoFAST8 PRIoFAST8
#define CC_PRIxFAST8 PRIxFAST8
#define CC_PRIXFAST8 PRIXFAST8

#define CC_PRIdFAST16 PRIdFAST16
#define CC_PRIiFAST16 PRIiFAST16
#define CC_PRIuFAST16 PRIuFAST16
#define CC_PRIoFAST16 PRIoFAST16
#define CC_PRIxFAST16 PRIxFAST16
#define CC_PRIXFAST16 PRIXFAST16

#define CC_PRIdFAST32 PRIdFAST32
#define CC_PRIiFAST32 PRIiFAST32
#define CC_PRIuFAST32 PRIuFAST32
#define CC_PRIoFAST32 PRIoFAST32
#define CC_PRIxFAST32 PRIxFAST32
#define CC_PRIXFAST32 PRIXFAST32
#else
 /* Fall back on C89's/C++98's dumb types. */
typedef signed char cc_s8l;
typedef signed short cc_s16l;
typedef signed long cc_s32l;

typedef unsigned char cc_u8l;
typedef unsigned short cc_u16l;
typedef unsigned long cc_u32l;

typedef signed int cc_s8f;
typedef signed int cc_s16f;
typedef signed long cc_s32f;

typedef unsigned int cc_u8f;
typedef unsigned int cc_u16f;
typedef unsigned long cc_u32f;

#define CC_PRIdLEAST8 "%d"
#define CC_PRIiLEAST8 "%i"
#define CC_PRIuLEAST8 "%u"
#define CC_PRIoLEAST8 "%o"
#define CC_PRIxLEAST8 "%x"
#define CC_PRIXLEAST8 "%X"

#define CC_PRIdLEAST16 "%d"
#define CC_PRIiLEAST16 "%i"
#define CC_PRIuLEAST16 "%u"
#define CC_PRIoLEAST16 "%o"
#define CC_PRIxLEAST16 "%x"
#define CC_PRIXLEAST16 "%X"

#define CC_PRIdLEAST32 "%ld"
#define CC_PRIiLEAST32 "%li"
#define CC_PRIuLEAST32 "%lu"
#define CC_PRIoLEAST32 "%lo"
#define CC_PRIxLEAST32 "%lx"
#define CC_PRIXLEAST32 "%lX"

#define CC_PRIdFAST8 "%d"
#define CC_PRIiFAST8 "%i"
#define CC_PRIuFAST8 "%u"
#define CC_PRIoFAST8 "%o"
#define CC_PRIxFAST8 "%x"
#define CC_PRIXFAST8 "%X"

#define CC_PRIdFAST16 "%d"
#define CC_PRIiFAST16 "%i"
#define CC_PRIuFAST16 "%u"
#define CC_PRIoFAST16 "%o"
#define CC_PRIxFAST16 "%x"
#define CC_PRIXFAST16 "%X"

#define CC_PRIdFAST32 "%ld"
#define CC_PRIiFAST32 "%li"
#define CC_PRIuFAST32 "%lu"
#define CC_PRIoFAST32 "%lo"
#define CC_PRIxFAST32 "%lx"
#define CC_PRIXFAST32 "%lX"
#endif

/* Boolean. */
typedef cc_u8l cc_bool;
enum
{
	cc_false = 0,
	cc_true = 1
};

#endif

/* MSVC is extraordinarily stupid and does not define '__cplusplus' to the proper value. */
/* Standards exist for a reason, dumbasses. */
#ifdef __cplusplus
 #ifndef CLOWNRESAMPLER_CPLUSPLUS
  #ifdef _MSVC_LANG
   #define CLOWNRESAMPLER_CPLUSPLUS _MSVC_LANG
  #else
   #define CLOWNRESAMPLER_CPLUSPLUS __cplusplus
  #endif
 #endif
#endif

#define CLOWNRESAMPLER_COUNT_OF(x) (sizeof(x) / sizeof(*(x)))
#define CLOWNRESAMPLER_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLOWNRESAMPLER_MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLOWNRESAMPLER_CLAMP(min, max, x) (CLOWNRESAMPLER_MAX((min), CLOWNRESAMPLER_MIN((max), (x))))

#define CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE (1L << 16) /* For 16.16. This is good because it reduces multiplications and divisions to mere bit-shifts. */
#define CLOWNRESAMPLER_TO_FIXED_POINT_FROM_INTEGER(x) ((x) * CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE)
#define CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_FLOOR(x) ((x) / CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE)
#define CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_ROUND(x) (((x) + (CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE / 2)) / CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE)
#define CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_CEILING(x) (((x) + (CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE - 1)) / CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE)
#define CLOWNRESAMPLER_FIXED_POINT_MULTIPLY(a, b) ((a) * (b) / CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE)

typedef struct ClownResampler_Precomputed
{
	cc_s32l lanczos_kernel_table[CLOWNRESAMPLER_KERNEL_RADIUS * 2 * CLOWNRESAMPLER_KERNEL_RESOLUTION];
} ClownResampler_Precomputed;

typedef struct ClownResampler_LowestLevel_Configuration
{
	size_t stretched_kernel_radius;         /* 16.16 fixed point. */
	size_t integer_stretched_kernel_radius;
	size_t stretched_kernel_radius_delta;   /* 16.16 fixed point. */
	size_t kernel_step_size;
} ClownResampler_LowestLevel_Configuration;

typedef struct ClownResampler_LowLevel_State
{
	ClownResampler_LowestLevel_Configuration lowest_level;

	cc_u8f channels;
	size_t position_integer;
	cc_u32f position_fractional;            /* 16.16 fixed point. */
	cc_u32f increment;                      /* 16.16 fixed point. */
} ClownResampler_LowLevel_State;

typedef struct ClownResampler_HighLevel_State
{
	ClownResampler_LowLevel_State low_level;

	cc_s16l input_buffer[0x1000]; /* TODO: This should be dynamically allocated in accordance with the kernel radius... */
	cc_s16l *input_buffer_start;
	cc_s16l *input_buffer_end;
	size_t maximum_integer_stretched_kernel_radius;
	size_t leading_padding_frames_needed, trailing_padding_frames_remaining;
} ClownResampler_HighLevel_State;

typedef size_t (*ClownResampler_InputCallback)(void *user_data, cc_s16l *buffer, size_t total_frames);
typedef cc_bool (*ClownResampler_OutputCallback)(void *user_data, const cc_s32f *frame, cc_u8f total_samples);

#endif /* CLOWNRESAMPLER_GUARD_MISC */

#if !defined(CLOWNRESAMPLER_STATIC) || defined(CLOWNRESAMPLER_IMPLEMENTATION)

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CLOWNRESAMPLER_GUARD_FUNCTION_DECLARATIONS
#define CLOWNRESAMPLER_GUARD_FUNCTION_DECLARATIONS
/* Common API.
   This API is used for both the low-level and high-level APIs. */

/* Precomputes some data to improve the performance of the resampler.
   Multiple resamplers can use the same 'ClownResampler_Precomputed'.
   The output of this function is always the same, so if you want to avoid
   calling this function, then you could dump the contents of the struct and
   then insert a const 'ClownResampler_Precomputed' in your source code. */
CLOWNRESAMPLER_API void ClownResampler_Precompute(ClownResampler_Precomputed *precomputed);



/* Lowest-level API. */
CLOWNRESAMPLER_API cc_bool ClownResampler_LowestLevel_Configure(ClownResampler_LowestLevel_Configuration *configuration, cc_u32f input_sample_rate, cc_u32f output_sample_rate, cc_u32f low_pass_filter_sample_rate);
CLOWNRESAMPLER_API void ClownResampler_LowestLevel_Resample(const ClownResampler_LowestLevel_Configuration *configuration, const ClownResampler_Precomputed *precomputed, cc_s32f *output_frame, cc_u8f channels, const cc_s16l *input_buffer, size_t position_integer, cc_u32f position_fractional);

#endif /* CLOWNRESAMPLER_GUARD_FUNCTION_DECLARATIONS */



#ifndef CLOWNRESAMPLER_NO_LOW_LEVEL_API
/* Low-level API.
   This API has lower overhead, but is more difficult to use, requiring that
   audio be pre-processed before resampling.
   Do NOT mix low-level API calls with high-level API calls for the same
   resampler! */


/* Initialises a low-level resampler. This function must be called before the
   state is passed to any other functions. The input and output sample rates do
   not actually have to match the sample rates being used - they just need to
   provide the ratio between the two (for example, 1 and 2 works just as well
   as 22050 and 44100). Remember that a sample rate is double the frequency.
   The 'channels' parameter must not be larger than
   CLOWNRESAMPLER_MAXIMUM_CHANNELS.

   Returns 'cc_false' on failure, and 'cc_true' otherwise. */
CLOWNRESAMPLER_API cc_bool ClownResampler_LowLevel_Init(ClownResampler_LowLevel_State *resampler, cc_u8f channels, cc_u32f input_sample_rate, cc_u32f output_sample_rate, cc_u32f low_pass_filter_sample_rate);

/* Adjusts properties of the resampler. The input and output sample rates do
   not actually have to match the sample rates being used - they just need to
   provide the ratio between the two (for example, 1 and 2 works just as well
   as 22050 and 44100). Remember that a sample rate is double the frequency.

   Returns 'cc_false' on failure, and 'cc_true' otherwise. */
CLOWNRESAMPLER_API cc_bool ClownResampler_LowLevel_Adjust(ClownResampler_LowLevel_State *resampler, cc_u32f input_sample_rate, cc_u32f output_sample_rate, cc_u32f low_pass_filter_sample_rate);

/* Resamples (pre-processed) audio. The 'total_input_frames' and
   'total_output_frames' parameters measure the size of their respective
   buffers in frames, not samples nor bytes.

   The input buffer must be specially pre-processed, so that it is padded with
   extra frames at the beginning and end. This is needed as the resampler will
   unavoidably read past the beginning and the end of the audio data. The
   specific number of frames needed at the beginning and end can be found in
   the 'resampler->integer_stretched_kernel_radius' variable. If the audio you
   are resampling is a chunk of a larger piece of audio, then the 'padding' at
   the beginning and end must be the frames from before and after said chunk
   of audio, otherwise these frames should just be 0. Note that these padding
   frames must not be counted by the 'total_input_frames' parameter.

   'output_callback' is a callback for outputting a single completed frame.
   'frame' points to a series of samples corresponding a frame of audio.
   'total_samples' is the number of samples in the frame, which will always
   match the number of channels that was passed to
   'ClownResampler_LowLevel_Init'. Must return 0 if no more frames are needed,
   in which case this function terminates. The 'user_data' parameter is the
   same as the 'user_data' parameter of this function.

   After this function returns, the 'total_input_frames' parameter will
   contain the number of frames in the input buffer that were not processed.

   This function will return 'cc_true' if it terminated because it ran out of
   input samples, or 'cc_false' if it terminated because the callback returned
   0. */
CLOWNRESAMPLER_API cc_bool ClownResampler_LowLevel_Resample(ClownResampler_LowLevel_State *resampler, const ClownResampler_Precomputed *precomputed, const cc_s16l *input_buffer, size_t *total_input_frames, ClownResampler_OutputCallback output_callback, const void *user_data);
#endif /* CLOWNRESAMPLER_NO_LOW_LEVEL_API */



#ifndef CLOWNRESAMPLER_NO_HIGH_LEVEL_API
/* High-level API.
   This API has more overhead, but is easier to use.
   Do NOT mix high-level API calls with low-level API calls for the same
   resampler! */


/* Initialises a high-level resampler. This function must be called before the
   state is passed to any other functions. The input and output sample rates do
   not actually have to match the sample rates being used - they just need to
   provide the ratio between the two (for example, 1 and 2 works just as well
   as 22050 and 44100). Remember that a sample rate is double the frequency.
   The 'channels' parameter must not be larger than
   CLOWNRESAMPLER_MAXIMUM_CHANNELS.

   Returns 'cc_false' on failure, and 'cc_true' otherwise. */
CLOWNRESAMPLER_API cc_bool ClownResampler_HighLevel_Init(ClownResampler_HighLevel_State *resampler, cc_u8f channels, cc_u32f input_sample_rate, cc_u32f output_sample_rate, cc_u32f low_pass_filter_sample_rate);

/* Resamples audio. This function returns when either the output buffer is
   full, or the input callback stops providing frames.

   This function will return 'cc_true' if it terminated because the input
   callback returned 0, or 'cc_false' if it terminated because the output
   callback returned 0.

   The parameters are as follows:

   'resampler'

   A pointer to a state struct that was previously initialised with the
   'ClownResampler_HighLevel_Init' function.


   'output_buffer'

   A pointer to a buffer which the resampled audio will be written to.
   The size of the audio buffer will be specified by the 'total_output_frames'
   variable.


   'total_output_frames'

   The size of the buffer specified by the 'output_buffer' parameter. The size
   is measured in frames, not samples nor bytes.


   'input_callback'

   A callback for retrieving frames of the input audio. The callback must
   write frames to the buffer pointed to by the 'buffer' parameter. The
   'total_frames' parameter specifies the maximum number of frames that can be
   written to the buffer. The callback must return the number of frames that
   were written to the buffer. If the callback returns 0, then this function
   terminates. The 'user_data' parameter is the same as the 'user_data'
   parameter of this function.


   'output_callback'


   A callback for outputting a single completed frame. 'frame' points to a
   series of samples corresponding a frame of audio. 'total_samples' is the
   number of samples in the frame, which will always match the number of
   channels that was passed to 'ClownResampler_HighLevel_Init'. Must return 0
   if no more frames are needed, in which case this function terminates. The
   'user_data' parameter is the same as the 'user_data' parameter of this
   function.


   'user_data'
   An arbitrary pointer that is passed to the callback functions. */
CLOWNRESAMPLER_API cc_bool ClownResampler_HighLevel_Resample(ClownResampler_HighLevel_State *resampler, const ClownResampler_Precomputed *precomputed, ClownResampler_InputCallback input_callback, ClownResampler_OutputCallback output_callback, const void *user_data);

/* Flush the internal sample buffer. */
CLOWNRESAMPLER_API void ClownResampler_HighLevel_Reset(ClownResampler_HighLevel_State *resampler);
#endif /* CLOWNRESAMPLER_NO_HIGH_LEVEL_API */

#if !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_ADJUST) && !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_API)
/* Adjusts properties of the resampler. The input and output sample rates do
   not actually have to match the sample rates being used - they just need to
   provide the ratio between the two (for example, 1 and 2 works just as well
   as 22050 and 44100). Remember that a sample rate is double the frequency.

   Unlike in the low-level API, when the input sample rate is higher than the
   output sample rate, the ratio between the two MUST NOT be wider than that
   of the rates passed to the 'ClownResampler_HighLevel_Init' function.

   Returns 'cc_false' on failure, and 'cc_true' otherwise. */
CLOWNRESAMPLER_API cc_bool ClownResampler_HighLevel_Adjust(ClownResampler_HighLevel_State *resampler, cc_u32f input_sample_rate, cc_u32f output_sample_rate, cc_u32f low_pass_filter_sample_rate);
#endif /* CLOWNRESAMPLER_NO_HIGH_LEVEL_ADJUST */

#if !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_RESAMPLE_END) && !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_API)
/* This is to be used after the final call to
  'ClownResampler_HighLevel_Resample', to output the last few samples.

  Returns 'cc_true' when the final sample has been output. */
CLOWNRESAMPLER_API cc_bool ClownResampler_HighLevel_ResampleEnd(ClownResampler_HighLevel_State *resampler, const ClownResampler_Precomputed *precomputed, ClownResampler_OutputCallback output_callback, const void *user_data);
#endif /* CLOWNRESAMPLER_NO_HIGH_LEVEL_RESAMPLE_END */

#ifdef __cplusplus
}
#endif

#ifndef CLOWNRESAMPLER_GUARD_CXX_API
#define CLOWNRESAMPLER_GUARD_CXX_API

#if defined(CLOWNRESAMPLER_CPLUSPLUS) && CLOWNRESAMPLER_CPLUSPLUS >= 201103L

#include <utility>

namespace ClownResampler
{
	namespace Internal
	{
		class Precomputed
		{
		private:
			ClownResampler_Precomputed precomputed;

		public:
			Precomputed()
			{
				ClownResampler_Precompute(&precomputed);
			}

			operator const ClownResampler_Precomputed*() const
			{
				return &precomputed;
			}
		};

		inline Precomputed precomputed;
	}

	class HighLevel
	{
	private:
		ClownResampler_HighLevel_State state;

	public:
		template<typename... Ts>
		void Init(Ts &&...args)
		{
			ClownResampler_HighLevel_Init(&state, std::forward<Ts>(args)...);
		}

		template<typename... Ts>
		void Adjust(Ts &&...args)
		{
			ClownResampler_HighLevel_Adjust(&state, std::forward<Ts>(args)...);
		}

		void Reset()
		{
			ClownResampler_HighLevel_Reset(&state);
		}

		template<typename T1, typename T2>
		bool Resample(const T1 &input_callback, const T2 &output_callback)
		{
			/* Crazed nonsense to deal with the clunky C API. */
			const struct
			{
				const void *input_callback, *output_callback;
			} CallbackData = {
					&input_callback,
					&output_callback
			};

			const auto &InputCallbackWrapper = [](void *user_data, cc_s16l *buffer, std::size_t total_frames) -> std::size_t
			{
				auto data_pointer = static_cast<decltype(CallbackData)*>(user_data);

				return (*reinterpret_cast<const T1*>(data_pointer->input_callback))(buffer, total_frames);
			};

			const auto &OutputCallbackWrapper = [](void *user_data, const cc_s32f *frame, cc_u8f total_samples) -> cc_bool
			{
				auto data_pointer = static_cast<decltype(CallbackData)*>(user_data);

				return (*reinterpret_cast<const T2*>(data_pointer->output_callback))(frame, total_samples);
			};

			return ClownResampler_HighLevel_Resample(&state, Internal::precomputed, InputCallbackWrapper, OutputCallbackWrapper, &CallbackData);
		}

		template<typename T>
		bool ResampleEnd(const T &output_callback)
		{
			/* Crazed nonsense to deal with the clunky C API. */
			const auto &OutputCallbackWrapper = [](void *user_data, const cc_s32f *frame, cc_u8f total_samples) -> cc_bool
			{
				return (*reinterpret_cast<const T*>(user_data))(frame, total_samples);
			};

			return ClownResampler_HighLevel_ResampleEnd(&state, Internal::precomputed, OutputCallbackWrapper, &output_callback);
		}
	};
}
#endif

#endif /* CLOWNRESAMPLER_GUARD_CXX_API */

#endif /* !defined(CLOWNRESAMPLER_STATIC) || defined(CLOWNRESAMPLER_IMPLEMENTATION) */


/* 4. Implementation */

#ifdef CLOWNRESAMPLER_IMPLEMENTATION

#ifndef CLOWNRESAMPLER_GUARD_FUNCTION_DEFINITIONS
#define CLOWNRESAMPLER_GUARD_FUNCTION_DEFINITIONS

/* These can be used to provide your own C standard library functions. */
#ifndef CLOWNRESAMPLER_ASSERT
#include <assert.h>
#define CLOWNRESAMPLER_ASSERT assert
#endif

#ifndef CLOWNRESAMPLER_FABS
#include <math.h>
#define CLOWNRESAMPLER_FABS fabs
#endif

#ifndef CLOWNRESAMPLER_SIN
#include <math.h>
#define CLOWNRESAMPLER_SIN sin
#endif

#ifndef CLOWNRESAMPLER_ZERO
#include <string.h>
#define CLOWNRESAMPLER_ZERO(buffer, size) memset(buffer, 0, size)
#endif

#ifndef CLOWNRESAMPLER_MEMMOVE
#include <string.h>
#define CLOWNRESAMPLER_MEMMOVE memmove
#endif

#include <stddef.h>

static double ClownResampler_LanczosKernel(const double x)
{
	const double kernel_radius = (double)CLOWNRESAMPLER_KERNEL_RADIUS;

	const double x_times_pi = x * 3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679; /* 100 digits should be good enough. */
	const double x_times_pi_divided_by_radius = x_times_pi / kernel_radius;

	/*CLOWNRESAMPLER_ASSERT(x != 0.0);*/
	if (x == 0.0)
		return 1.0;

	CLOWNRESAMPLER_ASSERT(CLOWNRESAMPLER_FABS(x) <= kernel_radius);
	/*if (CLOWNRESAMPLER_FABS(x) > kernel_radius)
		return 0.0f*/

	return (CLOWNRESAMPLER_SIN(x_times_pi) * CLOWNRESAMPLER_SIN(x_times_pi_divided_by_radius)) / (x_times_pi * x_times_pi_divided_by_radius);
}


/* Common API */

static cc_u32f ClownResampler_CalculateRatio(const cc_u32f a, const cc_u32f b)
{
	/* HAHAHA, I NEVER THOUGHT LONG DIVISION WOULD ACTUALLY COME IN HANDY! */
	cc_u32f upper, middle, lower, result;

	/* A hack to prevent crashes when either sample rate is 0. */
	if (a == 0 || b == 0)
		return 0xFFFFFFFF;

	/* As well as splitting the number into chunks of CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE
	   size, this sneakily also multiplies it by CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE. */
	upper = a / CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE;
	middle = a % CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE;
	lower = 0;

	/* Perform long division. */
	middle |= upper % b * CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE;
	upper /= b;

	lower |= middle % b * CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE;
	middle /= b;

	/*even_lower |= lower % b * CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE;*/ /* Nothing to feed the remainder into... */
	lower /= b;

	/* Detect overflow. */
	if (upper != 0 || middle >= CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE)
		return 0xFFFFFFFF;

	/* Merge the chunks back together. */
	result = 0;
	/*result += upper * ((cc_u32f)CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE * CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE);*/
	result += middle * CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE;
	result += lower;

	/* Detect underflow. */
	if (result == 0)
		return 1;

	return result;
}

CLOWNRESAMPLER_API void ClownResampler_Precompute(ClownResampler_Precomputed* const precomputed)
{
	size_t i;

	for (i = 0; i < CLOWNRESAMPLER_COUNT_OF(precomputed->lanczos_kernel_table); ++i)
		precomputed->lanczos_kernel_table[i] = (cc_s32l)CLOWNRESAMPLER_TO_FIXED_POINT_FROM_INTEGER(ClownResampler_LanczosKernel(((double)i / (double)CLOWNRESAMPLER_COUNT_OF(precomputed->lanczos_kernel_table) * 2.0 - 1.0) * (double)CLOWNRESAMPLER_KERNEL_RADIUS));
}

CLOWNRESAMPLER_API cc_bool ClownResampler_LowestLevel_Configure(ClownResampler_LowestLevel_Configuration* const configuration, const cc_u32f input_sample_rate, const cc_u32f output_sample_rate, const cc_u32f low_pass_filter_sample_rate)
{
	/* Determine the kernel scale. This is used to apply a low-pass filter. Not only is this something that the user may
	   explicitly request, but it is needed when downsampling to avoid artefacts. */
	/* Note that we do not ever want the kernel to be squished, but rather only stretched. */
	const cc_u32f actual_low_pass_sample_rate = CLOWNRESAMPLER_MIN(input_sample_rate, CLOWNRESAMPLER_MIN(output_sample_rate, low_pass_filter_sample_rate));
	const cc_u32f kernel_scale = ClownResampler_CalculateRatio(input_sample_rate, actual_low_pass_sample_rate);
	const cc_u32f inverse_kernel_scale = ClownResampler_CalculateRatio(actual_low_pass_sample_rate, input_sample_rate);

	/* Bail on crazy ratios. */
	/* TODO: Maybe use better ratio logic to raise this limit? */
	if (kernel_scale >= CLOWNRESAMPLER_TO_FIXED_POINT_FROM_INTEGER(0x1000))
		return cc_false;

	configuration->stretched_kernel_radius = CLOWNRESAMPLER_KERNEL_RADIUS * kernel_scale;
	configuration->integer_stretched_kernel_radius = CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_CEILING(configuration->stretched_kernel_radius);
	configuration->stretched_kernel_radius_delta = CLOWNRESAMPLER_TO_FIXED_POINT_FROM_INTEGER(configuration->integer_stretched_kernel_radius) - configuration->stretched_kernel_radius;
	CLOWNRESAMPLER_ASSERT(configuration->stretched_kernel_radius_delta < CLOWNRESAMPLER_TO_FIXED_POINT_FROM_INTEGER(1));
	configuration->kernel_step_size = CLOWNRESAMPLER_FIXED_POINT_MULTIPLY(CLOWNRESAMPLER_KERNEL_RESOLUTION, inverse_kernel_scale);

	return cc_true;
}

CLOWNRESAMPLER_API void ClownResampler_LowestLevel_Resample(const ClownResampler_LowestLevel_Configuration* const configuration, const ClownResampler_Precomputed* const precomputed, cc_s32f* const output_frame, const cc_u8f channels, const cc_s16l* const input_buffer, const size_t position_integer, const cc_u32f position_fractional)
{
	cc_u8f current_channel;
	size_t sample_index, kernel_index;
	cc_s32f sample_normaliser;

	/* Calculate the bounds of the kernel convolution. */
	const size_t min_relative = CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_CEILING(position_fractional + configuration->stretched_kernel_radius_delta);
	const size_t max_relative = CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_FLOOR(position_fractional + configuration->stretched_kernel_radius);
	const size_t min = (position_integer + min_relative) * channels;
	const size_t max = (position_integer + configuration->integer_stretched_kernel_radius + max_relative) * channels;

	/* Yes, I know this line is insane.
	   It is essentially a simplified and fixed-point version of this:
	   const size_t kernel_start = (size_t)(configuration->kernel_step_size * ((float)(min / channels) - position_if_it_were_a_float)); */
	const size_t kernel_start = CLOWNRESAMPLER_FIXED_POINT_MULTIPLY(configuration->kernel_step_size, (CLOWNRESAMPLER_TO_FIXED_POINT_FROM_INTEGER(min_relative) - position_fractional));

	CLOWNRESAMPLER_ASSERT(min_relative <= configuration->integer_stretched_kernel_radius);
	CLOWNRESAMPLER_ASSERT(max_relative <= configuration->integer_stretched_kernel_radius);

	sample_normaliser = 0;

	for (sample_index = min, kernel_index = kernel_start; sample_index < max; sample_index += channels, kernel_index += configuration->kernel_step_size)
	{
		cc_s32f kernel_value;

		CLOWNRESAMPLER_ASSERT(kernel_index < CLOWNRESAMPLER_COUNT_OF(precomputed->lanczos_kernel_table));

		/* The distance between the frames being output and the frames being read is the parameter to the Lanczos kernel. */
		kernel_value = (cc_s32f)precomputed->lanczos_kernel_table[kernel_index];
		sample_normaliser += kernel_value;

		/* Modulate the samples with the kernel and add them to the accumulators. */
		for (current_channel = 0; current_channel < channels; ++current_channel)
			output_frame[current_channel] += CLOWNRESAMPLER_FIXED_POINT_MULTIPLY((cc_s32f)input_buffer[sample_index + current_channel], kernel_value);
	}

	/* Compute 17.15 fixed-point reciprocal. */
	/* TODO: Anything but this; divisions are slow! */
	sample_normaliser = 0x80000000 / sample_normaliser;

	/* Normalise the samples. */
	for (current_channel = 0; current_channel < channels; ++current_channel)
	{
		/* Note that we use a 17.15 version of CLOWNRESAMPLER_FIXED_POINT_MULTIPLY here.
		   This is because, if we used a 16.16 normaliser, then there's a chance that the result
		   of the multiplication would overflow, causing popping. */
        output_frame[current_channel] = (output_frame[current_channel] * sample_normaliser) / (1 << 15);
	}
}

#endif /* CLOWNRESAMPLER_GUARD_FUNCTION_DEFINITIONS */

#if !defined(CLOWNRESAMPLER_NO_LOW_LEVEL_API) && !defined(CLOWNRESAMPLER_GUARD_LOW_LEVEL_API)
#define CLOWNRESAMPLER_GUARD_LOW_LEVEL_API

/* Low-Level API */

CLOWNRESAMPLER_API cc_bool ClownResampler_LowLevel_Init(ClownResampler_LowLevel_State* const resampler, const cc_u8f channels, const cc_u32f input_sample_rate, const cc_u32f output_sample_rate, const cc_u32f low_pass_filter_sample_rate)
{
	resampler->channels = channels;
	resampler->position_integer = 0;
	resampler->position_fractional = 0;
	return ClownResampler_LowLevel_Adjust(resampler, input_sample_rate, output_sample_rate, low_pass_filter_sample_rate);
}

CLOWNRESAMPLER_API cc_bool ClownResampler_LowLevel_Adjust(ClownResampler_LowLevel_State* const resampler, const cc_u32f input_sample_rate, const cc_u32f output_sample_rate, const cc_u32f low_pass_filter_sample_rate)
{
	resampler->increment = ClownResampler_CalculateRatio(input_sample_rate, output_sample_rate);
	return ClownResampler_LowestLevel_Configure(&resampler->lowest_level, input_sample_rate, output_sample_rate, low_pass_filter_sample_rate);
}

CLOWNRESAMPLER_API cc_bool ClownResampler_LowLevel_Resample(ClownResampler_LowLevel_State* const resampler, const ClownResampler_Precomputed* const precomputed, const cc_s16l* const input_buffer, size_t* const total_input_frames, const ClownResampler_OutputCallback output_callback, const void* const user_data)
{
	for (;;)
	{
		/* Check if we have reached the end of the input buffer. */
		if (resampler->position_integer >= *total_input_frames)
		{
			resampler->position_integer -= *total_input_frames;
			*total_input_frames = 0;
			return cc_true;
		}
		else
		{
			cc_s32f samples[CLOWNRESAMPLER_MAXIMUM_CHANNELS] = {0}; /* Sample accumulators. */

			ClownResampler_LowestLevel_Resample(&resampler->lowest_level, precomputed, samples, resampler->channels, input_buffer, resampler->position_integer, resampler->position_fractional);

			/* Increment input buffer position. */
			resampler->position_fractional += resampler->increment;
			resampler->position_integer += CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_FLOOR(resampler->position_fractional);
			resampler->position_fractional %= CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE;

			/* Output the samples. */
			if (!output_callback((void*)user_data, samples, resampler->channels))
			{
				/* We've reached the end of the output buffer. */
				const size_t delta = CLOWNRESAMPLER_MIN(resampler->position_integer, *total_input_frames);

				*total_input_frames -= delta;
				resampler->position_integer -= delta;
				return cc_false;
			}
		}
	}
}

#endif /* CLOWNRESAMPLER_NO_LOW_LEVEL_API */

#if !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_API) && !defined(CLOWNRESAMPLER_GUARD_HIGH_LEVEL_API)
#define CLOWNRESAMPLER_GUARD_HIGH_LEVEL_API

/* High-Level API */

CLOWNRESAMPLER_API cc_bool ClownResampler_HighLevel_Init(ClownResampler_HighLevel_State* const resampler, const cc_u8f channels, const cc_u32f input_sample_rate, const cc_u32f output_sample_rate, const cc_u32f low_pass_filter_sample_rate)
{
	if (channels > CLOWNRESAMPLER_MAXIMUM_CHANNELS)
		return cc_false;

	if (!ClownResampler_LowLevel_Init(&resampler->low_level, channels, input_sample_rate, output_sample_rate, low_pass_filter_sample_rate))
		return cc_false;

	resampler->maximum_integer_stretched_kernel_radius = resampler->low_level.lowest_level.integer_stretched_kernel_radius;

	ClownResampler_HighLevel_Reset(resampler);

	return cc_true;
}

CLOWNRESAMPLER_API cc_bool ClownResampler_HighLevel_Resample(ClownResampler_HighLevel_State* const resampler, const ClownResampler_Precomputed* const precomputed, const ClownResampler_InputCallback input_callback, const ClownResampler_OutputCallback output_callback, const void* const user_data)
{
	cc_bool reached_end_of_output_buffer = cc_false;

	const size_t maximum_radius_in_samples = resampler->maximum_integer_stretched_kernel_radius * resampler->low_level.channels;
	const size_t double_maximum_radius_in_samples = maximum_radius_in_samples * 2;

	while (resampler->leading_padding_frames_needed != 0)
	{
		cc_s16l* const buffer = &resampler->input_buffer[double_maximum_radius_in_samples - resampler->leading_padding_frames_needed * resampler->low_level.channels];
		const size_t frames_read = input_callback((void*)user_data, buffer, resampler->leading_padding_frames_needed);

		if (frames_read == 0)
			return cc_true;

		resampler->leading_padding_frames_needed -= frames_read;
	}

	do
	{
		/* If the input buffer is empty, refill it. */
		if (resampler->input_buffer_start == resampler->input_buffer_end)
		{
			/* It is hard to explain this step-by-step, but essentially there is a trick that we do here:
			   in order to avoid the resampler reading frames outside of the buffer, we have 'deadzones'
			   at each end of the buffer. When a new batch of frames is needed, the second deadzone is
			   copied over the first one, and the second is overwritten by the end of the new frames. */

			/* Move the end of the last batch of data to the start of the buffer */
			/* (memcpy will not work here since the copy may overlap). */
			CLOWNRESAMPLER_MEMMOVE(resampler->input_buffer, resampler->input_buffer_end - maximum_radius_in_samples, double_maximum_radius_in_samples * sizeof(*resampler->input_buffer));

			/* Obtain input frames (note that the new frames start after the frames we just copied). */
			resampler->input_buffer_start = resampler->input_buffer + maximum_radius_in_samples;
			resampler->input_buffer_end = resampler->input_buffer_start + input_callback((void*)user_data, resampler->input_buffer + double_maximum_radius_in_samples, (CLOWNRESAMPLER_COUNT_OF(resampler->input_buffer) - double_maximum_radius_in_samples) / resampler->low_level.channels) * resampler->low_level.channels;

			/* If the callback returns 0, then we must have reached the end of the input data, so quit. */
			if (resampler->input_buffer_start == resampler->input_buffer_end)
				return cc_true;
		}

		/* Call the actual resampler. */
		{
			size_t input_frames;

			const size_t radius_in_samples = resampler->low_level.lowest_level.integer_stretched_kernel_radius * resampler->low_level.channels;

			input_frames = (resampler->input_buffer_end - resampler->input_buffer_start) / resampler->low_level.channels;
			reached_end_of_output_buffer = ClownResampler_LowLevel_Resample(&resampler->low_level, precomputed, resampler->input_buffer_start - radius_in_samples, &input_frames, output_callback, user_data) == 0;

			/* Increment input and output pointers. */
			resampler->input_buffer_start = resampler->input_buffer_end - input_frames * resampler->low_level.channels;
		}
	} while (!reached_end_of_output_buffer);

	return cc_false;
}

CLOWNRESAMPLER_API void ClownResampler_HighLevel_Reset(ClownResampler_HighLevel_State* const resampler)
{
	resampler->leading_padding_frames_needed = resampler->trailing_padding_frames_remaining = resampler->maximum_integer_stretched_kernel_radius;

	/* Blank the width of the kernel's left side to zero, since there will not be previous data to occupy it yet. */
	CLOWNRESAMPLER_ZERO(resampler->input_buffer, resampler->maximum_integer_stretched_kernel_radius * resampler->low_level.channels * sizeof(*resampler->input_buffer));

	/* Initialise the pointers to point to the middle of the first (and newly-initialised) kernel. */
	resampler->input_buffer_start = resampler->input_buffer_end = resampler->input_buffer + resampler->maximum_integer_stretched_kernel_radius * resampler->low_level.channels;
}

#endif /* CLOWNRESAMPLER_NO_HIGH_LEVEL_API */

#if !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_ADJUST) && !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_API) && !defined(CLOWNRESAMPLER_GUARD_HIGH_LEVEL_ADJUST)
#define CLOWNRESAMPLER_GUARD_HIGH_LEVEL_ADJUST

CLOWNRESAMPLER_API cc_bool ClownResampler_HighLevel_Adjust(ClownResampler_HighLevel_State* const resampler, const cc_u32f input_sample_rate, const cc_u32f output_sample_rate, const cc_u32f low_pass_filter_sample_rate)
{
	/* TODO: Cache the inputs to `ClownResampler_LowLevel_Adjust` instead. */
	const ClownResampler_LowLevel_State state_backup = resampler->low_level;

	if (!ClownResampler_LowLevel_Adjust(&resampler->low_level, input_sample_rate, output_sample_rate, low_pass_filter_sample_rate))
	{
		resampler->low_level = state_backup;
		return cc_false;
	}

	/* Fail if the ratio is too large. See the warning in this function's documentation for more information. */
	if (resampler->low_level.lowest_level.integer_stretched_kernel_radius > resampler->maximum_integer_stretched_kernel_radius)
	{
		resampler->low_level = state_backup;
		return cc_false;
	}

	/* Freak-out if the ratio is so high that the kernel radius would exceed the size of the input buffer. */
	if (resampler->low_level.lowest_level.integer_stretched_kernel_radius * 2 >= CLOWNRESAMPLER_COUNT_OF(resampler->input_buffer) / resampler->low_level.channels)
	{
		resampler->low_level = state_backup;
		return cc_false;
	}

	return cc_true;
}

#endif /* CLOWNRESAMPLER_NO_HIGH_LEVEL_ADJUST */

#if !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_RESAMPLE_END) && !defined(CLOWNRESAMPLER_NO_HIGH_LEVEL_API) && !defined(CLOWNRESAMPLER_GUARD_HIGH_LEVEL_RESAMPLE_END)
#define CLOWNRESAMPLER_GUARD_HIGH_LEVEL_RESAMPLE_END

typedef struct ClownResampler_CallbackWrapperData
{
	ClownResampler_HighLevel_State *resampler;
	ClownResampler_OutputCallback output_callback;
	void *user_data;
} ClownResampler_CallbackWrapperData;

static size_t ClownResampler_PaddingCallback(void* const user_data, cc_s16l* const buffer, const size_t total_frames)
{
	const ClownResampler_CallbackWrapperData* const data = (ClownResampler_CallbackWrapperData*)user_data;
	const size_t frames_to_do = CLOWNRESAMPLER_MIN(total_frames, data->resampler->trailing_padding_frames_remaining);

	CLOWNRESAMPLER_ZERO(buffer, frames_to_do * data->resampler->low_level.channels * sizeof(*buffer));

	data->resampler->trailing_padding_frames_remaining -= frames_to_do;

	return frames_to_do;
}

static cc_bool ClownResampler_OutputCallbackWrapper(void* const user_data, const cc_s32f* const frame, const cc_u8f total_samples)
{
	const ClownResampler_CallbackWrapperData* const data = (ClownResampler_CallbackWrapperData*)user_data;

	return data->output_callback(data->user_data, frame, total_samples);
}

CLOWNRESAMPLER_API cc_bool ClownResampler_HighLevel_ResampleEnd(ClownResampler_HighLevel_State* const resampler, const ClownResampler_Precomputed* const precomputed, const ClownResampler_OutputCallback output_callback, const void* const user_data)
{
	ClownResampler_CallbackWrapperData data;
	data.resampler = resampler;
	data.output_callback = output_callback;
	data.user_data = (void*)user_data;

	return ClownResampler_HighLevel_Resample(resampler, precomputed, ClownResampler_PaddingCallback, ClownResampler_OutputCallbackWrapper, &data);
}

#endif /* CLOWNRESAMPLER_NO_HIGH_LEVEL_RESAMPLE_END */

#endif /* CLOWNRESAMPLER_IMPLEMENTATION */
