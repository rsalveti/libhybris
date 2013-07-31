/*
 * Copyright (C) 2013 Canonical Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authored by: Jim Hodapp <jim.hodapp@canonical.com>
 *              Ricardo Salveti de Araujo <ricardo.salveti@canonical.com>
 */

#include <hybris/media/media_compatibility_layer.h>
#include "direct_media_test.h"

#include <utils/Errors.h>

#include <hybris/surface_flinger/surface_flinger_compatibility_layer.h>

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/DataSource.h>
#include <ui/DisplayInfo.h>
#include <gui/ISurfaceComposer.h>
#include <binder/ProcessState.h>
#include <gui/SurfaceComposerClient.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace android;

static float DestWidth = 0.0, DestHeight = 0.0;
// Actual video dimmensions
static int Width = 360, Height = 848;

static GLfloat positionCoordinates[8];

MPlayer *mplayer = NULL;

void calculate_position_coordinates()
{
	// Assuming cropping output for now
	float x = 1, y = 1;

	// Black borders
	x = float(Width / DestWidth);
	y = float(Height / DestHeight);

	// Make the larger side be 1
	if (x > y) {
		y /= x;
		x = 1;
	} else {
		x /= y;
		y = 1;
	}

	positionCoordinates[0] = -x;
	positionCoordinates[1] = y;
	positionCoordinates[2] = -x;
	positionCoordinates[3] = -y;
	positionCoordinates[4] = x;
	positionCoordinates[5] = -y;
	positionCoordinates[6] = x;
	positionCoordinates[7] = y;
}

WindowRenderer::WindowRenderer(int width, int height)
	: mThreadCmd(CMD_IDLE)
{
	createThread(threadStart, this);
}

WindowRenderer::~WindowRenderer()
{
}

int WindowRenderer::threadStart(void* self)
{
	((WindowRenderer *)self)->glThread();
	return 0;
}

void WindowRenderer::glThread()
{
	printf("%s\n", __PRETTY_FUNCTION__);

	Mutex::Autolock autoLock(mLock);
}

struct ClientWithSurface
{
	SfClient* client;
	SfSurface* surface;
};

ClientWithSurface client_with_surface(bool setup_surface_with_egl)
{
	ClientWithSurface cs = ClientWithSurface();

	cs.client = sf_client_create();

	if (!cs.client) {
		printf("Problem creating client ... aborting now.");
		return cs;
	}

	static const size_t primary_display = 0;

	DestWidth = sf_get_display_width(primary_display);
	DestHeight = sf_get_display_height(primary_display);
	printf("Primary display width: %f, height: %f\n", DestWidth, DestHeight);

	SfSurfaceCreationParameters params = {
		0,
		0,
		(int) DestWidth,
		(int) DestHeight,
		-1, //PIXEL_FORMAT_RGBA_8888,
		INT_MAX,
		0.5f,
		setup_surface_with_egl, // Do not associate surface with egl, will be done by camera HAL
		"MediaCompatLayerTestSurface"
	};

	cs.surface = sf_surface_create(cs.client, &params);

	if (!cs.surface) {
		printf("Problem creating surface ... aborting now.");
		return cs;
	}

	sf_surface_make_current(cs.surface);

	return cs;
}

struct RenderData
{
	static const char *vertex_shader()
	{
		return
			"attribute vec4 a_position;                                  \n"
			"attribute vec2 a_texCoord;                                  \n"
			"uniform mat4 m_texMatrix;                                   \n"
			"varying vec2 v_texCoord;                                    \n"
			"varying float topDown;                                      \n"
			"void main()                                                 \n"
			"{                                                           \n"
			"   gl_Position = a_position;                                \n"
			"   v_texCoord = (m_texMatrix * vec4(a_texCoord, 0.0, 1.0)).xy;\n"
			"}                                                           \n";
	}

	static const char *fragment_shader()
	{
		return
			"#extension GL_OES_EGL_image_external : require      \n"
			"precision mediump float;                            \n"
			"varying vec2 v_texCoord;                            \n"
			"uniform samplerExternalOES s_texture;               \n"
			"void main()                                         \n"
			"{                                                   \n"
			"  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
			"}                                                   \n";
	}

	static GLuint loadShader(GLenum shaderType, const char* pSource)
	{
		GLuint shader = glCreateShader(shaderType);

		if (shader) {
			glShaderSource(shader, 1, &pSource, NULL);
			glCompileShader(shader);
			GLint compiled = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

			if (!compiled) {
				GLint infoLen = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
				if (infoLen) {
					char* buf = (char*) malloc(infoLen);
					if (buf) {
						glGetShaderInfoLog(shader, infoLen, NULL, buf);
						fprintf(stderr, "Could not compile shader %d:\n%s\n",
								shaderType, buf);
						free(buf);
					}
					glDeleteShader(shader);
					shader = 0;
				}
			}
		} else {
			printf("Error, during shader creation: %i\n", glGetError());
		}

		return shader;
	}

	static GLuint create_program(const char* pVertexSource, const char* pFragmentSource)
	{
		GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
		if (!vertexShader) {
			printf("vertex shader not compiled\n");
			return 0;
		}

		GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
		if (!pixelShader) {
			printf("frag shader not compiled\n");
			return 0;
		}

		GLuint program = glCreateProgram();
		if (program) {
			glAttachShader(program, vertexShader);
			glAttachShader(program, pixelShader);
			glLinkProgram(program);
			GLint linkStatus = GL_FALSE;
			glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);

			if (linkStatus != GL_TRUE) {
				GLint bufLength = 0;
				glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
				if (bufLength) {
					char* buf = (char*) malloc(bufLength);
					if (buf) {
						glGetProgramInfoLog(program, bufLength, NULL, buf);
						fprintf(stderr, "Could not link program:\n%s\n", buf);
						free(buf);
					}
				}
				glDeleteProgram(program);
				program = 0;
			}
		}

		return program;
	}

	RenderData() : program_object(create_program(vertex_shader(), fragment_shader()))
	{
		position_loc = glGetAttribLocation(program_object, "a_position");
		tex_coord_loc = glGetAttribLocation(program_object, "a_texCoord");
		sampler_loc = glGetUniformLocation(program_object, "s_texture");
		matrix_loc = glGetUniformLocation(program_object, "m_texMatrix");
	}

	// Handle to a program object
	GLuint program_object;
	// Attribute locations
	GLint position_loc;
	GLint tex_coord_loc;
	// Sampler location
	GLint sampler_loc;
	// Matrix location
	GLint matrix_loc;
};

struct SurfaceVars
{
	EGLDisplay display;
	EGLSurface surface;
	RenderData *render_data;
};

static int setup_video_texture(ClientWithSurface *cs, GLuint *preview_texture_id)
{
	assert(cs != NULL);
	assert(preview_texture_id != NULL);

	glGenTextures(1, preview_texture_id);
	glClearColor(0, 0, 0, 0);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	return 0;
}

static void print_gl_error(unsigned int line)
{
	GLint error = glGetError();
	printf("GL error: %#04x (line: %d)\n", error, line);
}

static int update_gl_buffer(SurfaceVars *sv)
{
	RenderData *render_data = sv->render_data;

	GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

	const GLfloat textureCoordinates[] = {
		1.0f,  1.0f,
		0.0f,  1.0f,
		0.0f,  0.0f,
		1.0f,  0.0f
	};

	calculate_position_coordinates();

	glClear(GL_COLOR_BUFFER_BIT);
	// Use the program object
	glUseProgram(render_data->program_object);
	// Enable attributes
	glEnableVertexAttribArray(render_data->position_loc);
	glEnableVertexAttribArray(render_data->tex_coord_loc);
	// Load the vertex position
	glVertexAttribPointer(render_data->position_loc,
			2,
			GL_FLOAT,
			GL_FALSE,
			0,
			positionCoordinates);
	// Load the texture coordinate
	glVertexAttribPointer(render_data->tex_coord_loc,
			2,
			GL_FLOAT,
			GL_FALSE,
			0,
			textureCoordinates);

	GLfloat matrix[16];
#if 1
	android_media_surface_texture_get_transformation_matrix(mplayer, matrix);
#else
	mplayer->player()->GetTransformationMatrixForSurfaceTexture(matrix);
#endif

	glUniformMatrix4fv(render_data->matrix_loc, 1, GL_FALSE, matrix);

	glActiveTexture(GL_TEXTURE0);
	// Set the sampler texture unit to 0
	glUniform1i(render_data->sampler_loc, 0);
	glUniform1i(render_data->matrix_loc, 0);
#if 1
	android_media_update_surface_texture(mplayer);
#else
	mplayer->player()->updateSurfaceTexture();
#endif
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	//glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
	glDisableVertexAttribArray(render_data->position_loc);
	glDisableVertexAttribArray(render_data->tex_coord_loc);

	eglSwapBuffers(sv->display, sv->surface);

	return 0;
}

void update_surface(void *context)
{
	SurfaceVars *sv = (SurfaceVars *)context;
	update_gl_buffer(sv);
}

void set_video_size_cb(int height, int width, void *context)
{
	printf("Video height: %d, width: %d\n", height, width);
	printf("Video dest height: %f, width: %f\n", DestHeight, DestWidth);

	Height = width;
	Width = height;
}

#if 0
struct FrameAvailableListener : public android::SurfaceTexture::FrameAvailableListener
{
public:
	FrameAvailableListener()
	{
	}

	// From android::SurfaceTexture::FrameAvailableListener
	void onFrameAvailable()
	{
		printf("%s\n", __PRETTY_FUNCTION__);
		assert(mplayer->player() != NULL);
		mplayer->player()->updateSurfaceTexture();
	}
};
#endif

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("Usage: direct_media_test <video_to_play>\n");
		return EXIT_FAILURE;
	}

	mplayer = android_media_new_player();
	if (mplayer == NULL) {
		printf("Problem creating new media player.\n");
		return EXIT_FAILURE;
	}

	// Set player event cb for when the video size is known:
	android_media_set_video_size_cb(mplayer, set_video_size_cb, NULL);
	//mplayer->player()->setVideoSizeCb(set_video_size_cb, NULL);

	printf("Setting data source to: %s.\n", argv[1]);

	//mplayer->player()->setDataSource(argv[1]);
	if (android_media_set_data_source(mplayer, argv[1]) != OK) {
		printf("Failed to set data source: %s\n", argv[1]);
		return EXIT_FAILURE;
	}

#if 1
	WindowRenderer renderer(DestWidth, DestHeight);

	printf("Creating EGL surface.\n");
	ClientWithSurface cs = client_with_surface(true /* Associate surface with egl. */);
	if (!cs.surface) {
		printf("Problem acquiring surface for preview");
		return EXIT_FAILURE;
	}

	printf("Creating GL texture.\n");
	GLuint preview_texture_id;
	SurfaceVars *sv = new SurfaceVars;
	sv->display = sf_client_get_egl_display(cs.client);
	sv->surface = sf_surface_get_egl_surface(cs.surface);

	sf_surface_make_current(cs.surface);
	if (setup_video_texture(&cs, &preview_texture_id) != OK) {
		printf("Problem setting up GL texture for video surface.\n");
		return EXIT_FAILURE;
	}

	sv->render_data = new RenderData;

	printf("GL id: %d\n", preview_texture_id);
#if 0
	// Create a new SurfaceTexture from the texture_id in synchronous mode
	// (don't wait on all data in the buffer)
	const bool allow_synchronous_mode = true;
	sp<SurfaceTexture> surface_texture = new android::SurfaceTexture(
			preview_texture_id,
			allow_synchronous_mode);
	//sp<FrameAvailableListener> frame_listener;
	//surface_texture->setFrameAvailableListener(frame_listener);
	mplayer->player()->setSurface(surface_texture);
#endif
	android_media_set_preview_texture(mplayer, preview_texture_id);
	//mplayer->player()->setVideoTextureNeedsUpdateCb(update_surface, sv);
#else
	sp<SurfaceComposerClient> composerClient;
	sp<SurfaceControl> control;
	composerClient = new SurfaceComposerClient;
	CHECK_EQ(composerClient->initCheck(), (status_t)OK);

	sp<IBinder> display(SurfaceComposerClient::getBuiltInDisplay(
				ISurfaceComposer::eDisplayIdMain));
	DisplayInfo info;
	SurfaceComposerClient::getDisplayInfo(display, &info);
	ssize_t displayWidth = info.w;
	ssize_t displayHeight = info.h;

	ALOGV("display is %ld x %ld\n", displayWidth, displayHeight);

	control = composerClient->createSurface(
			String8("A Surface"),
			displayWidth,
			displayHeight,
			PIXEL_FORMAT_RGB_565,
			0);

	CHECK(control != NULL);
	CHECK(control->isValid());

	SurfaceComposerClient::openGlobalTransaction();
	CHECK_EQ(control->setLayer(INT_MAX), (status_t)OK);
	CHECK_EQ(control->show(), (status_t)OK);
	SurfaceComposerClient::closeGlobalTransaction();

	sp<Surface> surface = control->getSurface();
	CHECK(surface != NULL);

	mplayer->player->setSurface(surface->getSurfaceTexture());
#endif

	printf("Starting video playback.\n");
	android_media_play(mplayer);

#if 1
	printf("Updating gl buffer continuously...\n");
	while (android_media_is_playing(mplayer)) {
		update_gl_buffer(sv);
	}

#else
	sleep(10);
#endif

	android_media_reset(mplayer);
	//android_media_stop(mplayer);

	delete sv->render_data;
	delete sv;

	delete mplayer;

	return EXIT_SUCCESS;
}
